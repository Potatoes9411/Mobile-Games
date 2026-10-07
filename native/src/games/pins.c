/* ===========================================================================
   PIN RESCUE - native port, rebuilt against Pull the Pin (Popcore)

   A glass vessel of chambers held shut by thin blue pins. Pull a pin and the
   balls behind it pour out under real circle physics: hundreds of small balls
   that pile, flow through funnels and settle. Grey balls take the colour of
   any coloured ball they touch, and a grey ball that reaches the cup fails the
   level. Bombs go off when a coloured ball touches them, and a bomb in the cup
   fails too. Fill the cup to 100% to clear.

   Physics is position based: integrate, then relax ball-ball and ball-wall
   overlaps a few times per 1/120 s step, then read velocity back from the
   position change. It stays stable however deep the pile gets, which is the
   case that matters here, and it is fully deterministic.

   Everything simulates in a fixed 540 x 780 design space; render maps it onto
   whatever portrait canvas the phone has.
   =========================================================================== */
#include "../pa.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define DW 540.0f
#define DH 780.0f
#define BR 5.6f              /* ball radius */
#define BOMB_R 17.0f
#define WALL_R 7.5f          /* half the glass tube thickness */
#define PIN_R 3.9f
#define RING_R 13.2f         /* ring handle: centreline radius and stroke */
#define RING_W 6.6f
#define VIEW_W 450.0f        /* design units across the screen width */
#define CORNER 30.0f         /* default rounding of vessel corners */
#define GRAV 1500.0f
#define ITERS 4
#define MAXB 640
#define MAXSEG 520
#define MAXPIN 8
#define MAXPOLY 24
#define MAXPP 96
#define MAXPART 900
#define VMAX 900.0f

enum { BK_COL, BK_GREY, BK_BOMB };
enum { ST_PLAY, ST_WIN, ST_FAIL };
enum { FAIL_NONE, FAIL_GREY, FAIL_BOMB, FAIL_SHORT, FAIL_BOOM };

typedef struct {
    float x, y, px, py, vx, vy, r;
    float flash;             /* colour-change pop, counts down */
    float fuse;              /* bombs: >0 once lit */
    float nx, ny;            /* last wall contact normal this step */
    unsigned char kind, col, alive, incup, hit, burnt;
} Ball;

typedef struct { float ax, ay, bx, by, r; } Seg;

typedef struct {
    float tx, ty, hx, hy;    /* tip (inside the glass) and handle end */
    float dx, dy, len;       /* unit vector tip -> handle */
    int   state;             /* 0 seated, 1 sliding, 2 gone */
    float t, off;
} Pin;

typedef struct { int n, fill; PA_Vec2 p[MAXPP]; } Poly;

typedef struct {
    float x, y, vx, vy, rot, vr, w, h, life, max;
    PA_Color col;
    unsigned char kind;      /* 0 confetti, 1 spark, 2 smoke, 3 ring, 4 blast cloud */
    unsigned char screen;    /* coordinates in screen px rather than design */
} Part;

/* ---------------------------------------------------------------- palette -- */
static const PA_Color BALL_COLS[8] = {
    0xFFF2392C, 0xFFFFB81F, 0xFF3FD35A, 0xFF2E9BF0,
    0xFFF25CB4, 0xFF9B5CF0, 0xFFFF7A1A, 0xFF1FD3B6
};
#define GREY_COL  0xFFBDBDBD
#define PIN_COL   0xFF1E7FD6
#define TUBE_COL  0xFFDCDEE0
#define TUBE_EDGE 0xFF9DA2A8
#define INK       0xFF2E3136

/* ------------------------------------------------------------ level data --
   Each level is a little opcode script in design coordinates.
     V x y ... Z       glass polyline, corners rounded; its outline is tinted
     W x y ... Z       glass polyline with no tint (dividers, ledges, pegs)
     F x y ... Z       tint only: the interior of a vessel built from pieces
     T kind n x y ... Z  n balls scattered inside a polygon
     P tx ty hx hy     pin: tip inside the glass, handle (ring) outside
     B kind x y w h n  n balls packed into the box (0 coloured, 1 grey)
     K x y             a bomb
     C cx              cup centre x (default 270)
     G goal            balls needed for 100%
     S n i ...         the solution: pin indices in pull order (demo + hint)
     X n i ...         a losing order, for the review captures
     E                 end                                                   */
#define V_ 1
#define W_ 2
#define P_ 3
#define B_ 4
#define K_ 5
#define C_ 6
#define G_ 7
#define S_ 8
#define X_ 9
#define F_ 10
#define T_ 11
#define E_ 0
#define Z_ 9999

/* The bottle neck most levels end in: two tube ends over the cup. */
#define NECK_L 236,560
#define NECK_R 304,560

/* 1: one pin, the tutorial */
static const float LV01[] = {
    V_, NECK_L, 236,500, 100,410, 100,40, 370,40, 370,410, 304,500, NECK_R, Z_,
    P_, 98,215, 402,215,
    B_, 0, 106,95,258,114, 200,
    G_, 100, S_, 1, 0, E_
};

/* 2: a pin on the diagonal - the pile rides a 45 degree floor */
static const float LV02[] = {
    V_, NECK_L, 236,500, 120,400, 120,40, 430,40, 430,400, 304,500, NECK_R, Z_,
    P_, 426,350, 106,30,
    T_, 0, 200, 156,48, 422,48, 422,312, Z_,
    G_, 100, S_, 1, 0, E_
};

/* 3: pegs - the oddly satisfying one */
static const float LV03[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 412,130, 95,130,
    B_, 0, 136,40,268,84, 170,
    W_, 175,195, 175,215, Z_, W_, 235,195, 235,215, Z_, W_, 295,195, 295,215, Z_, W_, 355,195, 355,215, Z_,
    W_, 205,255, 205,275, Z_, W_, 265,255, 265,275, Z_, W_, 325,255, 325,275, Z_, W_, 385,255, 385,275, Z_,
    W_, 175,315, 175,335, Z_, W_, 235,315, 235,335, Z_, W_, 295,315, 295,335, Z_, W_, 355,315, 355,335, Z_,
    W_, 205,375, 205,395, Z_, W_, 265,375, 265,395, Z_, W_, 325,375, 325,395, Z_,
    G_, 100, S_, 1, 0, E_
};

/* 4: the long way round - an offset L of glass, grey waiting in the drop */
static const float LV04[] = {
    V_, 356,560, 356,290, 80,250, 80,120, 424,120, 424,560, Z_,
    P_, 230,272, 230,86,
    P_, 352,480, 452,480,
    B_, 0, 88,128,136,112, 110,
    B_, 1, 364,320,52,150, 45,
    C_, 390,
    G_, 95, S_, 2, 0, 1, X_, 2, 1, 0, E_
};

/* 5: the leaning vessel, a pin through the middle */
static const float LV05[] = {
    V_, NECK_L, 236,500, 110,330, 100,250, 360,110, 430,130, 420,320, 304,500, NECK_R, Z_,
    P_, 106,320, 455,320,
    P_, 300,316, 300,108,
    W_, 300,262, 420,232, Z_,
    B_, 1, 116,240,180,74, 90,
    B_, 0, 306,130,108,96, 70,
    G_, 95, S_, 2, 1, 0, E_
};

/* 6: twin flasks over a shared hold */
static const float LV06[] = {
    V_, NECK_L, 236,480, 100,330, 100,60, 250,60, 250,240, 290,240, 290,60, 440,60, 440,330, 304,480, NECK_R, Z_,
    P_, 256,250, 84,250,
    P_, 284,250, 456,250,
    P_, 144,380, 452,380,
    B_, 1, 108,90,134,154, 100,
    B_, 0, 298,90,134,154, 100,
    G_, 105, S_, 3, 0, 1, 2, X_, 3, 2, 0, 1, E_
};

/* 7: bombs on top - drain the balls out from under them */
static const float LV07[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 128,170, 445,170,
    P_, 412,330, 95,330,
    K_, 200,140, K_, 270,130, K_, 340,140,
    B_, 0, 136,200,268,124, 190,
    G_, 105, S_, 1, 1, X_, 2, 0, 1, E_
};

/* 8: careful with the bomb */
static const float LV08[] = {
    V_, NECK_L, 236,500, 100,400, 100,40, 440,40, 440,400, 304,500, NECK_R, Z_,
    W_, 270,60, 270,294, Z_,
    P_, 274,300, 84,300,
    P_, 266,300, 456,300,
    K_, 185,270,
    B_, 0, 280,100,154,194, 200,
    G_, 105, S_, 1, 1, X_, 1, 0, E_
};

/* 9: a pin as a wall between grey and colour */
static const float LV09[] = {
    V_, NECK_L, 236,500, 100,400, 100,40, 440,40, 440,400, 304,500, NECK_R, Z_,
    P_, 270,326, 270,16,
    P_, 273,332, 84,332,
    P_, 267,332, 456,332,
    B_, 1, 106,120,154,206, 100,
    B_, 0, 280,120,154,206, 100,
    G_, 105, S_, 3, 0, 1, 2, X_, 3, 1, 0, 2, E_
};

/* 10: send the bomb out of the side door first */
static const float LV10[] = {
    F_, NECK_L, 236,500, 130,420, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, 62,326, 130,286, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, NECK_L, 236,500, 130,420, 130,372, 70,412, Z_,
    P_, 128,170, 445,170,
    P_, 100,396, 100,272,
    P_, 134,376, 450,256,
    K_, 180,320,
    B_, 0, 136,50,268,114, 200,
    G_, 100, S_, 3, 1, 2, 0, X_, 3, 0, 1, 2, E_
};

/* 11: a pocket of colour over a sea of grey */
static const float LV11[] = {
    V_, NECK_L, 236,500, 100,400, 100,40, 440,40, 440,400, 304,500, NECK_R, Z_,
    W_, 220,46, 220,150, Z_,
    P_, 226,154, 84,154,
    P_, 102,330, 456,330,
    B_, 0, 106,56,108,92, 40,
    B_, 1, 106,170,328,154, 160,
    G_, 100, S_, 2, 0, 1, E_
};

/* 12: two floors, either order */
static const float LV12[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 412,150, 95,150,
    P_, 128,300, 445,300,
    B_, 0, 136,50,268,94, 100,
    B_, 0, 136,200,268,94, 100,
    G_, 105, S_, 2, 1, 0, E_
};

/* 13: grey balls - colour them before they drop */
static const float LV13[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 128,170, 445,170,
    P_, 412,330, 95,330,
    B_, 0, 136,60,268,104, 90,
    B_, 1, 136,220,268,104, 110,
    G_, 105, S_, 2, 0, 1, E_
};

/* 14: colour splits over two grey wells */
static const float LV14[] = {
    V_, NECK_L, 236,500, 100,400, 100,40, 440,40, 440,400, 304,500, NECK_R, Z_,
    P_, 98,150, 456,150,
    B_, 0, 106,50,328,94, 150,
    W_, 270,190, 270,326, Z_,
    P_, 274,330, 84,330,
    P_, 266,330, 456,330,
    B_, 1, 106,230,158,94, 70,
    B_, 1, 282,230,152,94, 70,
    G_, 105, S_, 3, 0, 1, 2, X_, 3, 1, 0, 2, E_
};

/* 15: dump the grey and the bomb out of the side door */
static const float LV15[] = {
    F_, NECK_L, 236,500, 130,420, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, 62,326, 130,286, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, NECK_L, 236,500, 130,420, 130,372, 70,412, Z_,
    P_, 128,170, 445,170,
    P_, 100,396, 100,272,
    P_, 134,376, 450,256,
    B_, 1, 150,220,150,70, 60,
    K_, 210,190,
    B_, 0, 136,50,268,114, 160,
    G_, 100, S_, 3, 1, 2, 0, X_, 3, 2, 0, 1, E_
};

/* 16: mix two chambers in the hold before opening the floor */
static const float LV16[] = {
    V_, NECK_L, 236,500, 100,410, 100,40, 440,40, 440,410, 304,500, NECK_R, Z_,
    W_, 270,46, 270,196, Z_,
    P_, 274,200, 84,200,
    P_, 266,200, 456,200,
    P_, 98,370, 456,370,
    B_, 1, 106,60,158,134, 100,
    B_, 0, 282,60,152,134, 100,
    G_, 105, S_, 3, 0, 1, 2, X_, 3, 2, 0, 1, E_
};

/* 17: grey over pegs */
static const float LV17[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 412,120, 95,120,
    B_, 0, 136,40,268,74, 80,
    P_, 128,230, 445,230,
    B_, 1, 136,130,268,94, 110,
    W_, 175,285, 175,300, Z_, W_, 235,285, 235,300, Z_, W_, 295,285, 295,300, Z_, W_, 355,285, 355,300, Z_,
    W_, 205,340, 205,355, Z_, W_, 265,340, 265,355, Z_, W_, 325,340, 325,355, Z_, W_, 385,340, 385,355, Z_,
    W_, 235,395, 235,410, Z_, W_, 295,395, 295,410, Z_,
    G_, 105, S_, 2, 0, 1, E_
};

/* 18: three floors, top down */
static const float LV18[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 128,140, 445,140,
    P_, 412,260, 95,260,
    P_, 128,380, 445,380,
    B_, 0, 136,50,268,84, 70,
    B_, 1, 136,170,268,84, 65,
    B_, 1, 136,290,268,84, 65,
    G_, 105, S_, 3, 0, 1, 2, X_, 3, 2, 1, 0, E_
};

/* 19: bombs above, grey below, colour in the middle */
static const float LV19[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 128,130, 445,130,
    P_, 412,250, 95,250,
    P_, 128,380, 445,380,
    K_, 190,100, K_, 270,96, K_, 350,100,
    B_, 0, 136,150,268,94, 110,
    B_, 1, 136,300,268,74, 70,
    G_, 105, S_, 2, 1, 2, X_, 3, 0, 1, 2, E_
};

/* 20: zigzag - colour flows over the grey before the gate opens */
static const float LV20[] = {
    V_, NECK_L, 236,500, 130,410, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    P_, 412,120, 95,120,
    B_, 0, 136,40,268,74, 110,
    W_, 130,200, 330,250, Z_,
    W_, 410,300, 210,350, Z_,
    P_, 212,352, 98,262,
    B_, 1, 220,240,110,50, 45,
    G_, 95, S_, 2, 0, 1, E_
};

/* 21: colour on the left, a bomb on the right, grey in the hold */
static const float LV21[] = {
    V_, NECK_L, 236,500, 100,410, 100,40, 440,40, 440,410, 304,500, NECK_R, Z_,
    W_, 270,46, 270,186, Z_,
    P_, 274,190, 84,190,
    P_, 266,190, 456,190,
    P_, 98,370, 456,370,
    B_, 0, 106,60,158,124, 100,
    K_, 355,160,
    B_, 1, 106,280,328,84, 100,
    G_, 105, S_, 2, 0, 2, X_, 3, 1, 0, 2, E_
};

/* 22: the tower - colour, grey, colour, grey */
static const float LV22[] = {
    V_, NECK_L, 236,500, 180,430, 180,40, 360,40, 360,430, 304,500, NECK_R, Z_,
    P_, 178,130, 395,130,
    P_, 362,230, 145,230,
    P_, 178,330, 395,330,
    P_, 362,420, 145,420,
    B_, 0, 186,46,168,78, 55,
    B_, 1, 186,150,168,74, 55,
    B_, 0, 186,250,168,74, 55,
    B_, 1, 186,350,168,64, 55,
    G_, 105, S_, 4, 0, 1, 2, 3, X_, 4, 3, 2, 1, 0, E_
};

/* 23: twin flasks, one of them hiding a bomb */
static const float LV23[] = {
    V_, NECK_L, 236,480, 100,330, 100,60, 250,60, 250,240, 290,240, 290,60, 440,60, 440,330, 304,480, NECK_R, Z_,
    P_, 256,250, 84,250,
    P_, 284,250, 456,250,
    P_, 144,380, 452,380,
    B_, 0, 108,90,134,154, 100,
    K_, 380,220,
    B_, 1, 130,290,280,84, 100,
    G_, 105, S_, 2, 0, 2, X_, 3, 1, 0, 2, E_
};

/* 24: the bomb goes out the door before anything falls on it */
static const float LV24[] = {
    F_, NECK_L, 236,500, 130,420, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, 62,326, 130,286, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, NECK_L, 236,500, 130,420, 130,372, 70,412, Z_,
    P_, 128,110, 445,110,
    P_, 412,200, 95,200,
    P_, 100,396, 100,272,
    P_, 134,376, 450,256,
    B_, 0, 136,46,268,58, 100,
    B_, 1, 136,130,268,64, 90,
    K_, 180,320,
    G_, 100, S_, 4, 2, 3, 0, 1, X_, 4, 0, 1, 2, 3, E_
};

/* 25: everything at once */
static const float LV25[] = {
    F_, NECK_L, 236,500, 130,420, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, 62,326, 130,286, 130,40, 410,40, 410,410, 304,500, NECK_R, Z_,
    W_, NECK_L, 236,500, 130,420, 130,372, 70,412, Z_,
    W_, 270,46, 270,146, Z_,
    P_, 274,150, 98,150,
    P_, 266,150, 442,150,
    P_, 100,396, 100,272,
    P_, 134,376, 450,256,
    P_, 128,405, 445,405,
    B_, 0, 136,50,128,94, 85,
    B_, 1, 276,50,128,94, 85,
    K_, 200,320,
    G_, 95, S_, 5, 2, 3, 0, 1, 4, X_, 3, 0, 1, 4, E_
};

static const float *const LEVELS[] = { LV01, LV02, LV03, LV04, LV05, LV06, LV07, LV08, LV09, LV10, LV11, LV12, LV13,
                                       LV14, LV15, LV16, LV17, LV18, LV19, LV20, LV21, LV22, LV23, LV24, LV25 };
#define LEVEL_COUNT ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

/* ------------------------------------------------------------------ state -- */
static Ball  g_b[MAXB];
static int   g_nb;
static Seg   g_seg[MAXSEG];
static int   g_nseg;
static Pin   g_pin[MAXPIN];
static int   g_npin;
static Poly  g_poly[MAXPOLY];
static int   g_npoly;
static int   g_sol[MAXPIN], g_nsol;
static int   g_wrong[MAXPIN], g_nwrong;
static Part  g_part[MAXPART];
static int   g_npart;

static int   g_level;         /* 0-based level being played */
static int   g_layout;        /* which handcrafted layout that is */
static int   g_mirror;
static int   g_goal, g_count;
static float g_shown_pct;
static int   g_state, g_fail_reason;
static float g_state_t, g_time, g_goal_t, g_settle_t, g_shake;
static float g_cup_pulse, g_cup_bounce;
static float g_tick_cd, g_pop_cd;
static int   g_pulls;
static float g_boom_t;
static float g_kick;          /* blast shake, 0.15 s */        /* a bomb went off: the fail card follows the blast */
static float g_intro;         /* level-in fade, also gates sounds */
static int   g_live;          /* 0 while presettling: no sound, no particles */
static float g_hint_t;        /* >0 while the hint hand is showing */
static PA_Rng g_rng;

/* cup geometry: wall centre half-width, rim and floor heights */
static float g_cx, g_rim, g_bot, g_hw;

/* the hand: tutorial, hint button and demo all drive the same pointer */
static float g_hand_x, g_hand_y, g_hand_press;
static int   g_hand_target;   /* pin index or -1 */
static float g_tug;           /* tutorial: how far the hand tugs its pin, visual only */

/* demo */
static int   g_demo, g_demo_wrong, g_demo_step;
static float g_demo_wait;
static int   g_demo_phase;    /* 0 waiting, 1 hand travelling, 2 pressing */
static float g_demo_t;

/* screen transform, resolved in render and remembered for input */
static float g_s = 1.0f, g_ox, g_oy;
static int   g_cw = 540, g_ch = 1170;

#define GCELL 11.0f
#define GX0 -80.0f
#define GY0 -80.0f
#define GW 64
#define GH 96
static int grid_head[GW * GH];
static int grid_next[MAXB];

static float sx(float x) { return g_ox + x * g_s; }
static float sy(float y) { return g_oy + y * g_s; }

static int layout_for(int level) {
    if (level < LEVEL_COUNT) return level;
    /* Past the handcrafted run the later layouts come round again mirrored,
       so the numbering keeps climbing without repeating a screen verbatim. */
    int first = LEVEL_COUNT > 10 ? 8 : 0;
    int span = LEVEL_COUNT - first;
    return first + (level - LEVEL_COUNT) % span;
}

static Part *spawn_part(float x, float y, float vx, float vy, PA_Color col, int kind,
                       float size, float life, int screen) {
    if (g_npart >= MAXPART || !g_live) return NULL;
    Part *p = &g_part[g_npart++];
    memset(p, 0, sizeof(*p));
    p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->col = col; p->kind = (unsigned char)kind;
    p->w = size; p->h = size * (kind == 0 ? 0.55f : 1.0f);
    p->life = p->max = life; p->screen = (unsigned char)screen;
    p->rot = pa_rng_range(&g_rng, 0, PA_TAU);
    p->vr = pa_rng_range(&g_rng, -9.0f, 9.0f);
    return p;
}

/* ------------------------------------------------------------- level load -- */
static float mx(float x) { return g_mirror ? DW - x : x; }

static void add_seg(float ax, float ay, float bx, float by, float r) {
    if (g_nseg >= MAXSEG) return;
    Seg *s = &g_seg[g_nseg++];
    s->ax = ax; s->ay = ay; s->bx = bx; s->by = by; s->r = r;
}

/* Round every interior corner of a polyline with a short quadratic arc, the
   bent-tube look of the reference, and the same curve feeds the physics. */
static void round_poly(const PA_Vec2 *in, int n, Poly *out) {
    out->n = 0;
    for (int i = 0; i < n; i++) {
        if (i == 0 || i == n - 1 || out->n + 8 >= MAXPP) { out->p[out->n++] = in[i]; continue; }
        PA_Vec2 a = in[i - 1], b = in[i], c = in[i + 1];
        float l1 = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        float l2 = sqrtf((c.x - b.x) * (c.x - b.x) + (c.y - b.y) * (c.y - b.y));
        float r = CORNER;
        if (r > l1 * 0.45f) r = l1 * 0.45f;
        if (r > l2 * 0.45f) r = l2 * 0.45f;
        if (r < 2.0f || l1 < 1e-3f || l2 < 1e-3f) { out->p[out->n++] = b; continue; }
        PA_Vec2 p0 = { b.x + (a.x - b.x) / l1 * r, b.y + (a.y - b.y) / l1 * r };
        PA_Vec2 p2 = { b.x + (c.x - b.x) / l2 * r, b.y + (c.y - b.y) / l2 * r };
        for (int k = 0; k <= 6; k++) {
            float t = (float)k / 6.0f, u = 1.0f - t;
            PA_Vec2 q = { u * u * p0.x + 2 * u * t * b.x + t * t * p2.x,
                          u * u * p0.y + 2 * u * t * b.y + t * t * p2.y };
            out->p[out->n++] = q;
        }
    }
}

static void add_ball(int kind, float x, float y, float r) {
    if (g_nb >= MAXB) return;
    Ball *b = &g_b[g_nb++];
    memset(b, 0, sizeof(*b));
    b->x = b->px = x; b->y = b->py = y; b->r = r;
    b->kind = (unsigned char)kind;
    b->col = (unsigned char)pa_rng_int(&g_rng, 0, 7);
    b->alive = 1;
}

/* Fill a polygon (design coordinates, before mirroring) with n balls. Slots
   come off a loose hex lattice, are ranked bottom-up with a random lift so
   the top of the pile comes out ragged, and each one is jittered by up to
   0.3 of a ball, so a level never starts as a packed rectangle. */
static int point_in_poly(const PA_Vec2 *p, int n, float x, float y) {
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((p[i].y > y) != (p[j].y > y)) &&
            (x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) in = !in;
    return in;
}

static void add_balls_poly(int kind, const PA_Vec2 *poly, int np, int n) {
    static float slot_x[2400], slot_y[2400], slot_k[2400];
    float d = BR * 2.06f, rowh = d * 0.88f;
    float lo = 1e9f, hi = -1e9f, top = 1e9f, bot = -1e9f;
    for (int i = 0; i < np; i++) {
        if (poly[i].x < lo) lo = poly[i].x;
        if (poly[i].x > hi) hi = poly[i].x;
        if (poly[i].y < top) top = poly[i].y;
        if (poly[i].y > bot) bot = poly[i].y;
    }
    int ns = 0;
    for (int row = 0; ns < 2400; row++) {
        float yy = bot - BR - (float)row * rowh;
        if (yy < top + BR * 0.5f) break;
        for (float xx = lo + BR + ((row & 1) ? d * 0.5f : 0.0f); xx <= hi - BR && ns < 2400; xx += d) {
            if (!point_in_poly(poly, np, xx, yy)) continue;
            slot_x[ns] = xx; slot_y[ns] = yy;
            slot_k[ns] = (float)row + pa_rng_next(&g_rng) * 2.6f;
            ns++;
        }
    }
    for (int placed = 0; placed < n && placed < ns; placed++) {
        int best = placed;
        for (int i = placed + 1; i < ns; i++) if (slot_k[i] < slot_k[best]) best = i;
        float tx = slot_x[best], ty = slot_y[best], tk = slot_k[best];
        slot_x[best] = slot_x[placed]; slot_y[best] = slot_y[placed]; slot_k[best] = slot_k[placed];
        slot_x[placed] = tx; slot_y[placed] = ty; slot_k[placed] = tk;
        add_ball(kind, mx(tx + pa_rng_range(&g_rng, -0.3f, 0.3f) * d), ty + pa_rng_range(&g_rng, -0.15f, 0.15f) * d,
                 BR * pa_rng_range(&g_rng, 0.94f, 1.06f));
    }
}

static void add_balls(int kind, float x, float y, float w, float h, int n) {
    PA_Vec2 box[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
    add_balls_poly(kind, box, 4, n);
}

static void build_cup(void) {
    g_rim = 600.0f; g_bot = 704.0f; g_hw = 58.0f;
    add_seg(g_cx - g_hw, g_rim - 2.0f, g_cx - g_hw, g_bot, 4.0f);
    add_seg(g_cx - g_hw, g_bot, g_cx + g_hw, g_bot, 4.0f);
    add_seg(g_cx + g_hw, g_bot, g_cx + g_hw, g_rim - 2.0f, 4.0f);
}

static void presettle(float seconds);
static void bank_stats(void);
static int  g_run_pulls, g_run_greys, g_banked;
static PA_Color g_pal[8];
static void ring_pos(const Pin *p, float *x, float *y);

static void load_level(int level) {
    g_level = level;
    g_layout = layout_for(level);
    g_mirror = level >= LEVEL_COUNT;
    const float *s = LEVELS[g_layout];
    g_nb = g_nseg = g_npin = g_npoly = g_nsol = g_nwrong = g_npart = 0;
    g_goal = 10; g_cx = 270.0f;
    pa_rng_seed(&g_rng, 0x51A7u + (uint32_t)level * 7919u);

    int i = 0;
    while ((int)s[i] != E_) {
        int op = (int)s[i++];
        if (op == V_ || op == W_ || op == F_) {
            int n = 0;
            PA_Vec2 raw[48];
            while (s[i] != Z_) {
                if (n < 48) { raw[n].x = mx(s[i]); raw[n].y = s[i + 1]; n++; }
                i += 2;
            }
            i++;
            if (g_npoly >= MAXPOLY) continue;
            Poly *p = &g_poly[g_npoly++];
            round_poly(raw, n < 48 ? n : 48, p);
            p->fill = op == V_ ? 1 : op == F_ ? 2 : 0;
            if (op == F_) continue;
            for (int k = 0; k + 1 < p->n; k++) add_seg(p->p[k].x, p->p[k].y, p->p[k + 1].x, p->p[k + 1].y, WALL_R);
            if (p->n == 1) add_seg(p->p[0].x, p->p[0].y, p->p[0].x, p->p[0].y, WALL_R);
        } else if (op == P_) {
            Pin *p = &g_pin[g_npin < MAXPIN ? g_npin++ : MAXPIN - 1];
            memset(p, 0, sizeof(*p));
            p->tx = mx(s[i]); p->ty = s[i + 1]; p->hx = mx(s[i + 2]); p->hy = s[i + 3];
            float dx = p->hx - p->tx, dy = p->hy - p->ty;
            p->len = sqrtf(dx * dx + dy * dy);
            p->dx = dx / p->len; p->dy = dy / p->len;
            i += 4;
        } else if (op == B_) {
            int kind = (int)s[i];
            float x = s[i + 1], y = s[i + 2], w = s[i + 3], h = s[i + 4];
            add_balls(kind, x, y, w, h, (int)s[i + 5]);
            i += 6;
        } else if (op == T_) {
            int kind = (int)s[i], n = (int)s[i + 1];
            i += 2;
            PA_Vec2 poly[16];
            int np = 0;
            while (s[i] != Z_) { if (np < 16) { poly[np].x = s[i]; poly[np].y = s[i + 1]; np++; } i += 2; }
            i++;
            add_balls_poly(kind, poly, np, n);
        } else if (op == K_) {
            add_ball(BK_BOMB, mx(s[i]), s[i + 1], BOMB_R);
            i += 2;
        } else if (op == C_) {
            g_cx = mx(s[i++]);
        } else if (op == G_) {
            g_goal = (int)s[i++];
        } else if (op == X_) {
            int n = (int)s[i++];
            for (int k = 0; k < n; k++) if (g_nwrong < MAXPIN) g_wrong[g_nwrong++] = (int)s[i + k];
            i += n;
        } else if (op == S_) {
            int n = (int)s[i++];
            for (int k = 0; k < n; k++) if (g_nsol < MAXPIN) g_sol[g_nsol++] = (int)s[i + k];
            i += n;
        } else break;
    }
    build_cup();

    g_count = 0; g_shown_pct = 0;
    g_state = ST_PLAY; g_state_t = 0; g_fail_reason = FAIL_NONE;
    g_goal_t = -1.0f; g_settle_t = 0; g_shake = 0; g_cup_pulse = g_cup_bounce = 0;
    g_pulls = 0; g_boom_t = 0; g_kick = 0; g_run_pulls = g_run_greys = 0; g_banked = 0; g_intro = 0; g_hint_t = 0; g_time = 0;
    g_demo_step = 0; g_demo_wait = 0.8f; g_demo_phase = 0;
    g_hand_x = DW + 80.0f; g_hand_y = DH * 0.75f; g_hand_press = 0; g_tug = 0; g_hand_target = -1;
    presettle(1.4f);
    if (level == 0 && g_npin > 0) {
        g_hint_t = 1e6f; g_hand_target = 0;
        ring_pos(&g_pin[0], &g_hand_x, &g_hand_y);
    }
}

/* ---------------------------------------------------------------- physics -- */
static void closest_on_seg(float ax, float ay, float bx, float by, float x, float y,
                           float *ox, float *oy) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
    t = pa_clamp01(t);
    *ox = ax + dx * t; *oy = ay + dy * t;
}

static void collide_capsule(Ball *b, float ax, float ay, float bx, float by, float r) {
    /* cheap reject on the segment's bounding box */
    float rr = r + b->r;
    float lo = ax < bx ? ax : bx, hi = ax < bx ? bx : ax;
    if (b->x < lo - rr || b->x > hi + rr) return;
    lo = ay < by ? ay : by; hi = ay < by ? by : ay;
    if (b->y < lo - rr || b->y > hi + rr) return;
    float cx, cy;
    closest_on_seg(ax, ay, bx, by, b->x, b->y, &cx, &cy);
    float dx = b->x - cx, dy = b->y - cy;
    float d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) return;
    float d = sqrtf(d2);
    if (d < 1e-4f) { dx = 0; dy = -1; d = 1; }
    float push = (rr - d) / d;
    b->x += dx * push; b->y += dy * push;
    b->nx = dx / d; b->ny = dy / d; b->hit = 1;
}

static void pin_points(const Pin *p, float *ax, float *ay, float *bx, float *by) {
    float o = p->off;
    *ax = p->tx + p->dx * o; *ay = p->ty + p->dy * o;
    *bx = p->hx + p->dx * o; *by = p->hy + p->dy * o;
}

static void light_bomb(Ball *b) {
    if (b->fuse > 0) return;
    b->fuse = 0.22f;
    if (g_live) pa_tone(1800, 2400, 0.12f, 2, 0.03f);
}

/* The blast: a 0.35-screen burst of red puffs that swell over 0.4 s, a short
   hard shake, and every ball caught in it scorched grey for good. */
static void explode(Ball *bomb) {
    bomb->alive = 0;
    float R = 0.175f * VIEW_W, push = R * 1.7f;
    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive) continue;
        float dx = b->x - bomb->x, dy = b->y - bomb->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (b->kind == BK_BOMB) { if (d < R + 20.0f) b->fuse = b->fuse > 0 ? b->fuse : 0.08f; continue; }
        if (d < R * 0.85f) {
            b->kind = BK_GREY; b->burnt = 1; b->flash = 0.3f;
        }
        if (d < push) {
            float k = (1.0f - d / push) * 900.0f / (d + 1.0f);
            b->vx += dx * k; b->vy += dy * k - 140.0f * (1.0f - d / push);
        }
    }
    for (int k = 0; k < 18; k++) {
        float a = pa_rng_range(&g_rng, 0, PA_TAU), r = sqrtf(pa_rng_next(&g_rng)) * R * 0.5f;
        Part *p = spawn_part(bomb->x + cosf(a) * r, bomb->y + sinf(a) * r, cosf(a) * 60.0f, sinf(a) * 60.0f - 15.0f,
                             pa_mix(pa_hex(0xC81E1E), pa_hex(0xFF6B5B), pa_rng_next(&g_rng)),
                             4, R * 0.20f, pa_rng_range(&g_rng, 0.9f, 1.15f), 0);
        if (p) p->h = R * pa_rng_range(&g_rng, 0.40f, 0.55f);
    }
    for (int k = 0; k < 10; k++)
        spawn_part(bomb->x + pa_rng_range(&g_rng, -18, 18), bomb->y + pa_rng_range(&g_rng, -18, 18),
                   pa_rng_range(&g_rng, -25, 25), pa_rng_range(&g_rng, -60, -15), pa_hex(0xBDBDBD), 2,
                   pa_rng_range(&g_rng, 6, 11), pa_rng_range(&g_rng, 1.0f, 1.5f), 0);
    g_kick = 0.15f;
    if (g_live) {
        pa_sfx("boom");
        if (g_state == ST_PLAY && g_boom_t <= 0) g_boom_t = 1.1f;
    }
}

static void contact(Ball *a, Ball *b) {
    if (a->kind == b->kind || !g_live) return;
    if (a->kind == BK_BOMB) { if (b->kind == BK_COL) light_bomb(a); return; }
    if (b->kind == BK_BOMB) { if (a->kind == BK_COL) light_bomb(b); return; }
    Ball *g = a->kind == BK_GREY ? a : b;
    Ball *c = a->kind == BK_GREY ? b : a;
    /* A ball that has only just turned cannot pass it on yet, so colour
       sweeps through a grey pile as a visible wave instead of in one step. */
    if (c->flash > 0.30f - 0.035f || g->burnt) return;
    g->kind = BK_COL; g->col = (unsigned char)((c->col + 1 + pa_rng_int(&g_rng, 0, 6)) % 8); g->flash = 0.3f;
    spawn_part(g->x, g->y, 0, 0, g_pal[g->col], 3, BR, 0.25f, 0);
    g_run_greys++;
    if (g_live && g_pop_cd <= 0) { pa_tone(980, 1500, 0.04f, 0, 0.03f); g_pop_cd = 0.05f; }
}

static void physics_step(float dt) {
    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive) continue;
        b->vy += GRAV * dt;
        b->px = b->x; b->py = b->y;
        b->x += b->vx * dt; b->y += b->vy * dt;
        b->hit = 0;
        if (b->flash > 0) b->flash -= dt;
    }

    /* broad phase: one bucket grid per step (bombs go in the same grid and
       search a wider neighbourhood) */
    for (int k = 0; k < GW * GH; k++) grid_head[k] = -1;
    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive) continue;
        int gx = (int)((b->x - GX0) / GCELL), gy = (int)((b->y - GY0) / GCELL);
        gx = gx < 0 ? 0 : gx >= GW ? GW - 1 : gx;
        gy = gy < 0 ? 0 : gy >= GH ? GH - 1 : gy;
        grid_next[i] = grid_head[gy * GW + gx];
        grid_head[gy * GW + gx] = i;
    }

    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < g_nb; i++) {
            Ball *a = &g_b[i];
            if (!a->alive) continue;
            int gx = (int)((a->x - GX0) / GCELL), gy = (int)((a->y - GY0) / GCELL);
            int reach = a->kind == BK_BOMB ? 4 : 1;
            for (int oy = -reach; oy <= reach; oy++) {
                int yy = gy + oy;
                if (yy < 0 || yy >= GH) continue;
                for (int ox = -reach; ox <= reach; ox++) {
                    int xx = gx + ox;
                    if (xx < 0 || xx >= GW) continue;
                    for (int j = grid_head[yy * GW + xx]; j >= 0; j = grid_next[j]) {
                        Ball *b = &g_b[j];
                        if (j == i || !b->alive) continue;
                        /* pairs are visited once: the smaller index owns it,
                           unless the other one is a bomb, which owns its own */
                        if (b->kind == BK_BOMB && a->kind != BK_BOMB) continue;
                        if (j < i && (a->kind == BK_BOMB) == (b->kind == BK_BOMB)) continue;
                        float dx = b->x - a->x, dy = b->y - a->y;
                        float rr = a->r + b->r;
                        float d2 = dx * dx + dy * dy;
                        if (d2 >= rr * rr) {
                            /* touching counts a hair before overlap, so colour
                               spreads through a pile that is merely resting */
                            if (it == 0 && a->kind != b->kind && d2 < (rr + 1.2f) * (rr + 1.2f)) contact(a, b);
                            continue;
                        }
                        float d = sqrtf(d2);
                        if (d < 1e-4f) { dx = 0.01f; dy = 1; d = 1; }
                        /* bombs are heavy: they shoulder balls aside */
                        float wa = 0.5f, wb = 0.5f;
                        if (a->kind == BK_BOMB && b->kind != BK_BOMB) { wa = 0.15f; wb = 0.85f; }
                        float pen = (rr - d) / d;
                        a->x -= dx * pen * wa; a->y -= dy * pen * wa;
                        b->x += dx * pen * wb; b->y += dy * pen * wb;
                        if (it == 0 && a->kind != b->kind) contact(a, b);
                    }
                }
            }
        }
        for (int i = 0; i < g_nb; i++) {
            Ball *b = &g_b[i];
            if (!b->alive) continue;
            for (int k = 0; k < g_nseg; k++) {
                Seg *s = &g_seg[k];
                collide_capsule(b, s->ax, s->ay, s->bx, s->by, s->r);
            }
            for (int k = 0; k < g_npin; k++) {
                Pin *p = &g_pin[k];
                if (p->state == 2) continue;
                float ax, ay, bx, by;
                pin_points(p, &ax, &ay, &bx, &by);
                collide_capsule(b, ax, ay, bx, by, PIN_R);
            }
        }
    }

    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive) continue;
        float vx = (b->x - b->px) / dt, vy = (b->y - b->py) / dt;
        if (b->hit) {
            /* restitution 0.3 off the glass and the pins, so a stream
               scatters where it lands instead of pouring like syrup */
            float vn0 = b->vx * b->nx + b->vy * b->ny;
            if (vn0 < -90.0f) {
                float vn1 = vx * b->nx + vy * b->ny;
                float k = -0.3f * vn0 - vn1;
                vx += b->nx * k; vy += b->ny * k;
            }
        }
        float sp2 = vx * vx + vy * vy;
        if (sp2 > VMAX * VMAX) { float k = VMAX / sqrtf(sp2); vx *= k; vy *= k; }
        b->vx = vx * 0.9995f; b->vy = vy * 0.9995f;
        if (b->y > DH + 260.0f || b->x < -120.0f || b->x > DW + 120.0f) b->alive = 0;
        if (b->kind == BK_BOMB && b->fuse > 0) {
            b->fuse -= dt;
            if (b->fuse <= 0) explode(b);
        }
    }
}

static void presettle(float seconds) {
    g_live = 0;
    int steps = (int)(seconds * 120.0f);
    for (int i = 0; i < steps; i++) physics_step(1.0f / 120.0f);
    for (int i = 0; i < g_nb; i++) { g_b[i].vx = g_b[i].vy = 0; g_b[i].flash = 0; g_b[i].fuse = 0; }
    g_npart = 0;
    g_live = 1;
}

static int in_cup(const Ball *b) {
    return b->y > g_rim + 3.0f && b->y < g_bot && fabsf(b->x - g_cx) < g_hw;
}

/* ------------------------------------------------------------- game flow -- */
static void save_progress(int next) {
    if (g_demo) return;
    if (next > pa_save_get("pins.level", 0)) pa_save_set("pins.level", next);
    pa_save_set("pins.current", next);
    pa_save_flush();
}

static void win(void) {
    g_state = ST_WIN; g_state_t = 0;
    save_progress(g_level + 1);
    bank_stats();
    pa_sfx("win");
    for (int k = 0; k < 170; k++) {
        float side = (k & 1) ? 1.0f : -1.0f;
        float x = side > 0 ? (float)g_cw + 10.0f : -10.0f;
        float y = (float)g_ch * pa_rng_range(&g_rng, 0.40f, 0.70f);
        spawn_part(x, y, -side * pa_rng_range(&g_rng, 160, 640), -pa_rng_range(&g_rng, 520, 1300),
                   BALL_COLS[k % 8], 0, pa_rng_range(&g_rng, 10, 17), pa_rng_range(&g_rng, 2.4f, 3.8f), 1);
    }
}

static void fail(int reason) {
    if (g_state != ST_PLAY) return;
    g_state = ST_FAIL; g_state_t = 0; g_fail_reason = reason;
    bank_stats();
    g_shake = g_shake > 0.3f ? g_shake : 0.3f;
    pa_sfx("lose");
}

static void pull_pin(int k) {
    Pin *p = &g_pin[k];
    if (p->state != 0 || g_state != ST_PLAY) return;
    p->state = 1; p->t = 0;
    g_pulls++; g_run_pulls++;
    g_shake = 0.08f;
    g_hint_t = 0;
    if (g_level == 0) g_hand_target = -1;
    pa_tone(1300, 2100, 0.06f, 1, 0.07f);
    pa_tone(700, 520, 0.05f, 0, 0.05f);
    for (int i = 0; i < 6; i++)
        spawn_part(p->hx, p->hy, pa_rng_range(&g_rng, -120, 120), pa_rng_range(&g_rng, -150, 50),
                   pa_hex(0x8CC8FF), 1, pa_rng_range(&g_rng, 1.8f, 3.2f), 0.4f, 0);
}

static void restart(void) { bank_stats(); pa_sfx("select"); load_level(g_level); }

static void next_level(void) {
    bank_stats();
    pa_sfx("select");
    load_level(g_level + 1);
}

/* the next pin of the solution that is still seated */
static int hint_pin(void) {
    for (int k = 0; k < g_nsol; k++) if (g_pin[g_sol[k]].state == 0) return g_sol[k];
    return -1;
}

/* --------------------------------------------------------------- HUD geo -- */
typedef struct { float x, y, w, h; } Box;

#define HUD_H 70.0f
/* the bar runs the full width less a 0.04 sw margin; on a landscape window
   it keeps a phone's proportions */
static void hud_span(float *x0, float *x1) {
    float m = (float)g_cw * 0.04f, w = (float)g_cw - 2.0f * m;
    if (w > 560.0f) w = 560.0f;
    *x0 = ((float)g_cw - w) * 0.5f; *x1 = *x0 + w;
}
enum { HUD_LEVEL, HUD_COLLECTION, HUD_ACHIEVE, HUD_HINT, HUD_SKINS, HUD_PAUSE, HUD_ICONS };
static float hud_icon_x(int k) {
    float x0, x1;
    hud_span(&x0, &x1);
    return x0 + (x1 - x0) * ((float)k + 0.5f) / (float)HUD_ICONS;
}
/* retry and skip sit on the studio floor, bottom left and right */
static Box floor_btn(int k) {
    float r = 30.0f, cy = (float)g_ch - 58.0f;
    float cx = k ? (float)g_cw * 0.88f : (float)g_cw * 0.12f;
    Box b = { cx - r, cy - r, r * 2.0f, r * 2.0f };
    return b;
}
static Box hud_btn(int k) {
    float cx = hud_icon_x(k), cy = HUD_H * 0.5f + 4.0f;
    Box b = { cx - 26.0f, cy - 30.0f, 52.0f, 60.0f };
    return b;
}
static Box card_btn(void) {
    float w = 250.0f, h = 76.0f;
    Box b = { ((float)g_cw - w) * 0.5f, (float)g_ch * 0.66f, w, h };
    return b;
}
static Box card_btn2(void) {
    Box b = card_btn();
    b.y += b.h + 30.0f; b.h = 44.0f; b.x += 40; b.w -= 80;
    return b;
}
static int in_box(Box b, float x, float y) {
    return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

static float seg_dist(float ax, float ay, float bx, float by, float x, float y) {
    float cx, cy;
    closest_on_seg(ax, ay, bx, by, x, y, &cx, &cy);
    return sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
}

static int pin_at(float px, float py) {
    float x = (px - g_ox) / g_s, y = (py - g_oy) / g_s;
    int best = -1;
    float bd = 30.0f / g_s;
    for (int k = 0; k < g_npin; k++) {
        Pin *p = &g_pin[k];
        if (p->state != 0) continue;
        float rx = p->hx + p->dx * RING_R * 1.6f, ry = p->hy + p->dy * RING_R * 1.6f;
        float d = seg_dist(p->tx, p->ty, rx, ry, x, y);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

/* ring centre of a pin, in design space */
static void ring_pos(const Pin *p, float *x, float *y) {
    float ax, ay, bx, by;
    pin_points(p, &ax, &ay, &bx, &by);
    *x = bx + p->dx * RING_R; *y = by + p->dy * RING_R;
}

/* ------------------------------------------------------------------ meta --
   Skins, stats, achievements and the picture collection, all saved under
   "pins.*". Run stats are banked when a level ends or is left. */
static const uint32_t SKINS[6][8] = {
    { 0xF2392C, 0xFFB81F, 0x3FD35A, 0x2E9BF0, 0xF25CB4, 0x9B5CF0, 0xFF7A1A, 0x1FD3B6 },
    { 0xFF8FB1, 0xFFC09F, 0xFFE38A, 0xA8E6A1, 0x9ED8F5, 0xC9A7F5, 0xF7A8D8, 0x8EE3D3 },
    { 0xFF2D95, 0x00E5FF, 0xB4FF00, 0xFFE600, 0x8A2BFF, 0xFF6A00, 0x00FF9C, 0xFF3D3D },
    { 0x0077B6, 0x00B4D8, 0x48CAE4, 0x90E0EF, 0x2EC4B6, 0x3A86FF, 0x5E60CE, 0x80FFDB },
    { 0xFF5E5B, 0xFFB347, 0xFFD166, 0xF77F00, 0xD62828, 0xFF8C94, 0xF4A261, 0xE76F51 },
    { 0x2A9D8F, 0x8AB17D, 0xE9C46A, 0x6A994E, 0x52B788, 0xA7C957, 0x99D98C, 0xF4A261 },
};
static const char *const SKIN_NAMES[6] = { "CONFETTI", "CANDY", "NEON", "OCEAN", "SUNSET", "MEADOW" };
static const int SKIN_UNLOCK[6] = { 0, 2, 5, 9, 14, 20 };

enum { ACH_COUNT = 6 };
static const char *const ACH_NAMES[ACH_COUNT] = { "FIRST POUR", "STEADY HANDS", "PIN PULLER", "BUCKET LIST", "COLOUR WAVE", "PIN MASTER" };
static const char *const ACH_DESC[ACH_COUNT] = { "CLEAR A LEVEL", "CLEAR 10 LEVELS", "PULL 100 PINS", "CUP 2000 BALLS", "COLOUR 1000 GREY BALLS", "CLEAR 25 LEVELS" };
static const int ACH_GOAL[ACH_COUNT] = { 1, 10, 100, 2000, 1000, 25 };

enum { PANEL_NONE, PANEL_COLLECTION, PANEL_ACHIEVE, PANEL_SKINS };
static int   g_panel;
static float g_panel_t;

static int cleared(void) { return pa_save_get("pins.level", 0); }

static void apply_skin(void) {
    int k = pa_save_get("pins.skin", 0);
    if (k < 0 || k >= 6 || cleared() < SKIN_UNLOCK[k]) k = 0;
    if (g_demo) k = 0;
    for (int i = 0; i < 8; i++) g_pal[i] = pa_hex(SKINS[k][i]);
}

static int ach_value(int a) {
    switch (a) {
        case 0: case 1: case 5: return cleared();
        case 2: return pa_save_get("pins.pulls", 0);
        case 3: return pa_save_get("pins.balls", 0);
        default: return pa_save_get("pins.greys", 0);
    }
}
static int ach_done(void) {
    int n = 0;
    for (int a = 0; a < ACH_COUNT; a++) if (ach_value(a) >= ACH_GOAL[a]) n++;
    return n;
}
static int skins_open(void) {
    int n = 0;
    for (int k = 0; k < 6; k++) if (cleared() >= SKIN_UNLOCK[k]) n++;
    return n;
}

/* bank this run's numbers once, whichever way the level is left */
static void bank_stats(void) {
    if (g_demo || g_banked) return;
    g_banked = 1;
    pa_save_set("pins.pulls", pa_save_get("pins.pulls", 0) + g_run_pulls);
    pa_save_set("pins.balls", pa_save_get("pins.balls", 0) + g_count);
    pa_save_set("pins.greys", pa_save_get("pins.greys", 0) + g_run_greys);
    pa_save_flush();
}

static int has_news(int panel) {
    if (g_demo) return panel != PANEL_ACHIEVE;   /* captures show the dots as a player sees them */
    if (panel == PANEL_COLLECTION) return cleared() > pa_save_get("pins.seen_pc", 0);
    if (panel == PANEL_SKINS) return skins_open() > pa_save_get("pins.seen_sk", 1);
    return ach_done() > pa_save_get("pins.seen_ac", 0);
}

static void open_panel(int which) {
    g_panel = which; g_panel_t = 0;
    pa_sfx("select");
    if (g_demo) return;
    if (which == PANEL_COLLECTION) pa_save_set("pins.seen_pc", cleared());
    if (which == PANEL_SKINS) pa_save_set("pins.seen_sk", skins_open());
    if (which == PANEL_ACHIEVE) pa_save_set("pins.seen_ac", ach_done());
    pa_save_flush();
}

static Box panel_card(void) {
    float w = (float)g_cw * 0.88f, h = (float)g_ch * 0.70f;
    if (h > 780.0f) h = 780.0f;
    if (w > 520.0f) w = 520.0f;
    Box b = { ((float)g_cw - w) * 0.5f, ((float)g_ch - h) * 0.5f + 20.0f, w, h };
    return b;
}
static Box panel_close(void) {
    Box c = panel_card();
    Box b = { c.x + c.w - 52.0f, c.y - 18.0f, 64.0f, 64.0f };
    return b;
}
static Box skin_tile(int k) {
    Box c = panel_card();
    float gap = 14.0f, tw = (c.w - 40.0f - gap) * 0.5f, th = (c.h - 150.0f - gap * 2.0f) / 3.0f;
    Box b = { c.x + 20.0f + (float)(k % 2) * (tw + gap), c.y + 110.0f + (float)(k / 2) * (th + gap), tw, th };
    return b;
}

static void panel_input(const PA_Input *in) {
    if (!in->pressed || g_panel_t < 0.15f) return;
    Box c = panel_card();
    if (in_box(panel_close(), in->x, in->y) || !in_box(c, in->x, in->y)) { g_panel = PANEL_NONE; pa_sfx("select"); return; }
    if (g_panel == PANEL_SKINS) {
        for (int k = 0; k < 6; k++) if (in_box(skin_tile(k), in->x, in->y)) {
            if (cleared() >= SKIN_UNLOCK[k]) {
                if (!g_demo) { pa_save_set("pins.skin", k); pa_save_flush(); }
                apply_skin();
                pa_tone(880, 1320, 0.08f, 1, 0.08f);
            } else pa_tone(300, 220, 0.12f, 3, 0.06f);
        }
    }
}


/* --------------------------------------------------------------- update -- */
static void s_start(void) {
    g_demo = pa_demo_mode();
    int lv;
    if (g_demo) {
        g_demo_wrong = g_demo >= 200;
        lv = (g_demo % 100) - 1;
        if (lv < 0) lv = 0;
    } else {
        g_demo_wrong = 0;
        lv = pa_save_get("pins.current", pa_save_get("pins.level", 0));
        if (lv < 0) lv = 0;
    }
    g_panel = PANEL_NONE;
    apply_skin();
    load_level(lv);
}

static void s_stop(void) {}

static void demo_drive(float dt) {
    int n = g_demo_wrong ? (g_nwrong ? g_nwrong : g_nsol) : g_nsol;
    if (g_state != ST_PLAY || g_demo_step >= n) return;
    int k = g_sol[g_demo_step];
    if (g_demo_wrong) k = g_nwrong ? g_wrong[g_demo_step] : g_sol[g_nsol - 1 - g_demo_step];
    if (g_demo_phase == 0) {
        g_demo_wait -= dt;
        if (g_demo_wait <= 0 && (g_settle_t > 0.3f || g_demo_wait < -1.8f)) {
            g_demo_phase = 1; g_demo_t = 0; g_hand_target = k;
        }
    } else if (g_demo_phase == 1) {
        g_demo_t += dt;
        if (g_demo_t > 0.45f) { g_demo_phase = 2; g_demo_t = 0; }
    } else {
        g_demo_t += dt;
        if (g_demo_t > 0.12f) {
            pull_pin(k);
            g_demo_step++;
            g_demo_phase = 0;
            g_demo_wait = 0.7f;
        }
    }
}

static void update_hand(float dt) {
    int target = g_hand_target;
    if (!g_demo && g_hint_t <= 0) target = -1;
    if (target >= 0 && g_pin[target].state != 0 && !(g_demo && g_demo_phase == 0 && g_pin[target].state == 1)) {
        if (!g_demo) target = -1;
    }
    float tx, ty, press = 0;
    if (target >= 0) {
        ring_pos(&g_pin[target], &tx, &ty);
        if (g_demo) press = g_demo_phase == 2 || g_pin[target].state == 1 ? 1.0f : 0.0f;
        else {
            /* tutorial loop, 1.2 s: grip the ring, drag it out along the pin
               (up to 0.3 of the screen, as far as the edge allows), let go */
            const Pin *p = &g_pin[target];
            float ph = fmodf(g_time, 1.2f);
            float room = 0.3f * VIEW_W;
            float ex = p->dx > 0 ? (DW + VIEW_W) * 0.5f - 14.0f - tx : p->dx < 0 ? tx - ((DW - VIEW_W) * 0.5f + 14.0f) : 1e9f;
            float ey = p->dy < 0 ? ty + 40.0f : 1e9f;
            if (fabsf(p->dx) > 0.01f && ex / fabsf(p->dx) < room) room = ex / fabsf(p->dx);
            if (p->dy < -0.01f && ey / fabsf(p->dy) < room) room = ey / fabsf(p->dy);
            if (room < 0) room = 0;
            float d = room * pa_smooth(pa_clamp01((ph - 0.25f) / 0.55f));
            press = ph > 0.12f && ph < 0.92f ? 1.0f : 0.0f;
            if (ph > 0.92f) d = room * (1.0f - pa_smooth((ph - 0.92f) / 0.28f));
            tx += p->dx * d; ty += p->dy * d;
            g_tug = d < 10.0f ? d : 10.0f;
            if (ph > 0.92f) g_tug = 0;
            g_hand_x = tx; g_hand_y = ty;
        }
    } else {
        tx = DW + 110.0f; ty = DH * 0.62f;
    }
    if (target < 0 || g_demo) g_tug = 0;
    float k = g_demo ? 9.0f : 6.0f;
    g_hand_x = pa_approach(g_hand_x, tx, k, dt);
    g_hand_y = pa_approach(g_hand_y, ty, k, dt);
    g_hand_press = pa_approach(g_hand_press, press, 22.0f, dt);
    if (g_hint_t > 0 && g_hint_t < 1e5f) { g_hint_t -= dt; if (g_hint_t <= 0) g_hand_target = -1; }
}

static void s_update(float dt, const PA_Input *in) {
    g_time += dt;
    g_state_t += dt;
    g_intro = pa_clamp01(g_intro + dt * 3.0f);
    if (g_shake > 0) g_shake = pa_approach(g_shake, 0, 5.0f, dt);
    if (g_kick > 0) g_kick -= dt;
    g_cup_pulse = pa_approach(g_cup_pulse, 0, 8.0f, dt);
    g_cup_bounce = pa_approach(g_cup_bounce, 0, 7.0f, dt);
    g_tick_cd -= dt; g_pop_cd -= dt;
    if (g_panel != PANEL_NONE) {         /* a panel is up: the level waits */
        g_panel_t += dt;
        panel_input(in);
        return;
    }

    if (in->pressed) {
        if (g_state == ST_PLAY) {
            if (in_box(floor_btn(0), in->x, in->y)) { restart(); return; }
            if (in_box(floor_btn(1), in->x, in->y)) { save_progress(g_level + 1); next_level(); return; }
            if (in_box(hud_btn(HUD_HINT), in->x, in->y)) {
                int h = hint_pin();
                if (h >= 0) { g_hand_target = h; g_hint_t = 3.6f; pa_sfx("select"); }
                return;
            }
            if (in_box(hud_btn(HUD_COLLECTION), in->x, in->y)) { open_panel(PANEL_COLLECTION); return; }
            if (in_box(hud_btn(HUD_ACHIEVE), in->x, in->y)) { open_panel(PANEL_ACHIEVE); return; }
            if (in_box(hud_btn(HUD_SKINS), in->x, in->y)) { open_panel(PANEL_SKINS); return; }
            int k = pin_at(in->x, in->y);
            if (k >= 0) pull_pin(k);
        } else if (g_state_t > 0.7f) {
            if (in_box(card_btn(), in->x, in->y)) {
                if (g_state == ST_WIN) next_level(); else restart();
                return;
            }
            if (g_state == ST_FAIL && in_box(card_btn2(), in->x, in->y)) {
                save_progress(g_level + 1); next_level(); return;
            }
        }
    }
    if (g_demo) demo_drive(dt);
    update_hand(dt);

    /* pins slide out on an accelerating ease, then drop out of the sim */
    for (int k = 0; k < g_npin; k++) {
        Pin *p = &g_pin[k];
        if (p->state != 1) continue;
        p->t += dt / 0.40f;
        float t = pa_clamp01(p->t);
        p->off = t * t * (2.4f - 1.4f * t) * (p->len + 30.0f);
        if (p->t >= 1.0f) p->state = 2;
    }

    physics_step(dt);

    int count = 0;
    float maxsp = 0;
    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive) { if (b->incup && b->kind == BK_COL) count++; continue; }
        float sp = b->vx * b->vx + b->vy * b->vy;
        if (sp > maxsp) maxsp = sp;
        if (!b->incup && in_cup(b)) {
            b->incup = 1;
            if (b->kind == BK_GREY) fail(FAIL_GREY);
            else if (b->kind == BK_BOMB) { b->fuse = 0.05f; fail(FAIL_BOMB); }
            else {
                g_cup_bounce = 1.0f;
                if (g_tick_cd <= 0) {
                    float f = 1000.0f + (float)(count < 80 ? count : 80) * 9.0f;
                    pa_tone(f, f * 1.25f, 0.03f, 0, 0.035f);
                    g_tick_cd = 0.04f;
                }
            }
        }
        if (b->incup && b->kind == BK_COL) count++;
    }
    if (count > g_count) g_cup_pulse = 1.0f;
    g_count = count;
    float pct = 100.0f * (float)g_count / (float)g_goal;
    g_shown_pct = pa_approach(g_shown_pct, pct, 10.0f, dt);

    int moving = 0;
    for (int k = 0; k < g_npin; k++) if (g_pin[k].state == 1) moving = 1;
    for (int i = 0; i < g_nb; i++) if (g_b[i].alive && (g_b[i].fuse > 0 || g_b[i].flash > 0)) moving = 1;
    if (!moving && maxsp < 30.0f * 30.0f) g_settle_t += dt; else g_settle_t = 0;

    if (g_boom_t > 0) {
        g_boom_t -= dt;
        if (g_boom_t <= 0) fail(FAIL_BOOM);
    }
    if (g_state == ST_PLAY && g_boom_t <= 0) {
        if (g_count >= g_goal) {
            if (g_goal_t < 0) g_goal_t = 0;
            g_goal_t += dt;
            if (g_goal_t > 0.5f) win();
        } else {
            /* Out of options: everything at rest and either no pins left or
               too few balls left to make the count. */
            int left = 0, potential = g_count;
            for (int k = 0; k < g_npin; k++) if (g_pin[k].state == 0) left++;
            for (int i = 0; i < g_nb; i++) {
                Ball *b = &g_b[i];
                if (b->alive && !b->incup && b->kind != BK_BOMB) potential++;
            }
            if (g_settle_t > 0.8f && (left == 0 || potential < g_goal)) fail(FAIL_SHORT);
        }
    }

    int w = 0;
    for (int i = 0; i < g_npart; i++) {
        Part *p = &g_part[i];
        p->life -= dt;
        if (p->life <= 0) continue;
        if (p->kind == 0) {
            p->vy += 900.0f * dt;
            p->vx *= 1.0f - 1.6f * dt; p->vy *= 1.0f - 1.6f * dt;
            p->rot += p->vr * dt;
        } else if (p->kind == 1) {
            p->vy += 600.0f * dt;
        } else if (p->kind == 2) {
            p->vx *= 1.0f - 2.5f * dt; p->vy *= 1.0f - 2.5f * dt;
            p->w += 8.0f * dt;
        } else if (p->kind == 4) {
            p->vx *= 1.0f - 4.0f * dt; p->vy *= 1.0f - 4.0f * dt;
            p->w = pa_approach(p->w, p->h, 9.0f, dt);
        }
        p->x += p->vx * dt; p->y += p->vy * dt;
        g_part[w++] = *p;
    }
    g_npart = w;
}

/* ---------------------------------------------------------------- render -- */
static void resolve_layout(PA_Canvas *c) {
    g_cw = c->w; g_ch = c->h;
    float W = (float)c->w, H = (float)c->h;
    /* The playfield is drawn big: VIEW_W design units span the phone's width,
       so a standard vessel fills about 0.7 of it as the reference does. */
    g_s = W / VIEW_W;
    float content = DH - 10.0f;                  /* ring tops to pill bottom */
    float avail = H - HUD_H - 16.0f;
    if (avail / content < g_s) g_s = avail / content;
    g_ox = (W - DW * g_s) * 0.5f;
    float spare = avail - content * g_s;
    g_oy = HUD_H + 8.0f + spare * 0.38f - 4.0f * g_s;
}

static void draw_background(PA_Canvas *c) {
    float w = (float)c->w, h = (float)c->h;
    /* studio cyclorama: a wall darkening toward the horizon, a lit floor */
    float hz = sy(g_bot + 70.0f);
    if (hz > h - 30.0f) hz = h - 30.0f;
    PA_Paint p = pa_linear(0, 0, 0, hz);
    pa_stop(&p, 0.0f, pa_hex(0xD2D2D2));
    pa_stop(&p, 0.30f, pa_hex(0xE1E1E1));
    pa_stop(&p, 0.70f, pa_hex(0xCFCFCF));
    pa_stop(&p, 1.0f, pa_hex(0xB9B9B9));
    pa_fill_rect_paint(c, 0, 0, w, hz + 2.0f, &p);
    PA_Paint f = pa_linear(0, hz, 0, h);
    pa_stop(&f, 0.0f, pa_hex(0xBDBDBD));
    pa_stop(&f, 0.10f, pa_hex(0xD3D3D3));
    pa_stop(&f, 0.45f, pa_hex(0xDEDEDE));
    pa_stop(&f, 1.0f, pa_hex(0xD2D2D2));
    pa_fill_rect_paint(c, 0, hz, w, h - hz, &f);
    /* the spot on the floor, and the soft roll of the cyc */
    PA_Paint sp = pa_radial(w * 0.5f, hz + (h - hz) * 0.35f, 0, w * 0.75f);
    pa_stop(&sp, 0.0f, PA_RGBA(255, 255, 255, 120));
    pa_stop(&sp, 1.0f, PA_RGBA(255, 255, 255, 0));
    pa_fill_ellipse_paint(c, w * 0.5f, hz + (h - hz) * 0.35f, w * 0.75f, (h - hz) * 0.6f, &sp);
    PA_Paint wall = pa_radial(w * 0.5f, h * 0.30f, 0, w * 0.7f);
    pa_stop(&wall, 0.0f, PA_RGBA(255, 255, 255, 90));
    pa_stop(&wall, 1.0f, PA_RGBA(255, 255, 255, 0));
    pa_fill_ellipse_paint(c, w * 0.5f, h * 0.30f, w * 0.7f, h * 0.36f, &wall);
    /* darker side walls */
    PA_Paint l = pa_linear(0, 0, w * 0.22f, 0);
    pa_stop(&l, 0.0f, PA_RGBA(90, 90, 96, 46));
    pa_stop(&l, 1.0f, PA_RGBA(90, 90, 96, 0));
    pa_fill_rect_paint(c, 0, 0, w * 0.22f, h, &l);
    PA_Paint r = pa_linear(w, 0, w * 0.78f, 0);
    pa_stop(&r, 0.0f, PA_RGBA(90, 90, 96, 46));
    pa_stop(&r, 1.0f, PA_RGBA(90, 90, 96, 0));
    pa_fill_rect_paint(c, w * 0.78f, 0, w * 0.22f, h, &r);
}

static void glass_fill(PA_Canvas *c, const Poly *f) {
    PA_Vec2 pts[MAXPP];
    float miny = 1e9f, maxy = -1e9f;
    for (int k = 0; k < f->n; k++) {
        pts[k].x = sx(f->p[k].x); pts[k].y = sy(f->p[k].y);
        if (pts[k].y < miny) miny = pts[k].y;
        if (pts[k].y > maxy) maxy = pts[k].y;
    }
    PA_Paint p = pa_linear(0, miny, 0, maxy);
    pa_stop(&p, 0.0f, PA_RGBA(255, 255, 255, 60));
    pa_stop(&p, 1.0f, PA_RGBA(255, 255, 255, 18));
    pa_fill_poly_paint(c, pts, f->n, &p);
}

/* The glass tube: a grey rim, a pale body, a white specular stripe up-left
   and a soft shadow line down-right, all following the same rounded path. */
/* The glass tube, as thick rounded plastic-glass: a soft drop shadow down and
   right, a grey rim, a pale body shaded darker toward its lower edge, and a
   white specular stripe a third of the wall wide along its upper-left side.
   `pass` 0 draws the shadow only (under the balls), 1 the tube itself. */
static void tube_stroke(PA_Canvas *c, const PA_Vec2 *dp, int n, float thick, int design, int pass) {
    PA_Vec2 pts[MAXPP], t[MAXPP];
    for (int k = 0; k < n; k++) {
        pts[k].x = design ? sx(dp[k].x) : dp[k].x;
        pts[k].y = design ? sy(dp[k].y) : dp[k].y;
    }
    float w = thick * (design ? g_s : 1.0f);
    if (n == 1) { pts[1] = pts[0]; n = 2; }
    if (pass == 0) {
        float off = (float)g_cw * 0.008f;
        if (!design) off = w * 0.3f;
        for (int k = 0; k < n; k++) { t[k].x = pts[k].x + off * 0.6f; t[k].y = pts[k].y + off; }
        pa_stroke_poly(c, t, n, 0, w * 1.05f, pa_hex(0xC3C5CC));
        pa_fill_circle(c, t[0].x, t[0].y, w * 0.525f, pa_hex(0xC3C5CC));
        pa_fill_circle(c, t[n - 1].x, t[n - 1].y, w * 0.525f, pa_hex(0xC3C5CC));
        return;
    }
    pa_stroke_poly(c, pts, n, 0, w + 2.0f, pa_hex(0xA4A8B2));
    pa_fill_circle(c, pts[0].x, pts[0].y, (w + 2.0f) * 0.5f, pa_hex(0xA4A8B2));
    pa_fill_circle(c, pts[n - 1].x, pts[n - 1].y, (w + 2.0f) * 0.5f, pa_hex(0xA4A8B2));
    pa_stroke_poly(c, pts, n, 0, w - 0.6f, pa_hex(0xD3D5DC));
    pa_fill_circle(c, pts[0].x, pts[0].y, (w - 0.6f) * 0.5f, pa_hex(0xD3D5DC));
    pa_fill_circle(c, pts[n - 1].x, pts[n - 1].y, (w - 0.6f) * 0.5f, pa_hex(0xD3D5DC));
    for (int k = 0; k < n; k++) { t[k].x = pts[k].x - w * 0.06f; t[k].y = pts[k].y - w * 0.08f; }
    pa_stroke_poly(c, t, n, 0, w * 0.72f, pa_hex(0xE6E7EC));
    pa_fill_circle(c, t[0].x, t[0].y, w * 0.36f, pa_hex(0xE6E7EC));
    pa_fill_circle(c, t[n - 1].x, t[n - 1].y, w * 0.36f, pa_hex(0xE6E7EC));
    for (int k = 0; k < n; k++) { t[k].x = pts[k].x - w * 0.17f; t[k].y = pts[k].y - w * 0.20f; }
    pa_stroke_poly(c, t, n, 0, w * 0.30f, pa_hex(0xFFFFFF));
    pa_fill_circle(c, t[0].x, t[0].y, w * 0.15f, pa_hex(0xFFFFFF));
    pa_fill_circle(c, t[n - 1].x, t[n - 1].y, w * 0.15f, pa_hex(0xFFFFFF));
}

static void draw_ball(PA_Canvas *c, float x, float y, float r, PA_Color col, float flash) {
    if (flash > 0) {
        float k = flash / 0.3f;
        r *= 1.0f + 0.25f * k;
        col = pa_mix(col, PA_RGB(255, 255, 255), 0.45f * k);
    }
    if (r < 2.2f) { pa_fill_circle(c, x, y, r, col); return; }
    /* a 1 px darker rim, a radial body falling 20% darker, a white spot */
    pa_fill_circle(c, x, y, r, pa_shade(col, -0.40f));
    float ri = r - 1.0f;
    PA_Paint p = pa_radial(x - ri * 0.35f, y - ri * 0.40f, 0.0f, ri * 1.40f);
    pa_stop(&p, 0.0f, pa_shade(col, 0.38f));
    pa_stop(&p, 0.38f, col);
    pa_stop(&p, 1.0f, pa_shade(col, -0.20f));
    pa_fill_ellipse_paint(c, x, y, ri, ri, &p);
    pa_fill_circle(c, x - r * 0.36f, y - r * 0.38f, r * 0.30f, PA_RGBA(255, 255, 255, 179));
}

/* The bomb: a glossy black sphere with a steel cap, a brown wick and a spark
   that always flickers at its tip, burning hot and fast once lit. */
static void draw_bomb(PA_Canvas *c, const Ball *b) {
    float x = sx(b->x), y = sy(b->y), r = b->r * g_s;
    float lit = b->fuse > 0 ? 1.0f : 0.0f;
    float pulse = lit * (0.5f + 0.5f * sinf(g_time * 60.0f));
    float fl = 0.5f + 0.5f * sinf(g_time * 23.0f + b->x * 0.3f);
    pa_shadow(c, x + r * 0.15f, y + r * 0.95f, r * 0.85f, r * 0.25f, 0.25f);
    float fw = (float)g_cw * 0.012f;
    PA_Vec2 wick[6] = { { x + r * 0.05f, y - r * 1.0f }, { x + r * 0.10f, y - r * 1.30f }, { x + r * 0.02f, y - r * 1.55f },
                        { x - r * 0.14f, y - r * 1.72f }, { x - r * 0.22f, y - r * 1.95f }, { x - r * 0.16f, y - r * 2.16f } };
    pa_stroke_poly(c, wick, 6, 0, fw + 1.5f, pa_hex(0x4A3020));
    pa_stroke_poly(c, wick, 6, 0, fw, pa_hex(0x8A5A34));
    pa_round_rect(c, x - r * 0.30f, y - r * 1.16f, r * 0.60f, r * 0.36f, r * 0.10f, pa_hex(0x55565C));
    pa_round_rect(c, x - r * 0.26f, y - r * 1.13f, r * 0.30f, r * 0.10f, r * 0.05f, pa_hex(0x8C8C94));
    /* spark */
    float sxp = wick[5].x, syp = wick[5].y;
    float sr = r * (0.26f + 0.10f * fl + 0.22f * lit);
    pa_fill_circle(c, sxp, syp, sr * 1.7f, PA_RGBA(255, 200, 58, (int)(70 + 60 * lit)));
    for (int k = 0; k < 6; k++) {
        float a = (float)k * PA_TAU / 6.0f + g_time * 9.0f;
        float l = sr * (1.1f + 0.5f * sinf(g_time * 31.0f + (float)k * 2.1f));
        pa_line(c, sxp, syp, sxp + cosf(a) * l, syp + sinf(a) * l, r * 0.06f + 0.8f, pa_hex(0xFFC83A));
    }
    pa_fill_circle(c, sxp, syp, sr * 0.55f, pa_hex(0xFFF2B0));
    PA_Color body = pa_mix(pa_hex(0x2A2A2E), pa_hex(0xD8261C), pulse * 0.6f);
    PA_Paint p = pa_radial(x - r * 0.35f, y - r * 0.40f, 0.0f, r * 1.5f);
    pa_stop(&p, 0.0f, pa_shade(body, 0.32f));
    pa_stop(&p, 0.45f, body);
    pa_stop(&p, 1.0f, pa_shade(body, -0.55f));
    pa_fill_ellipse_paint(c, x, y, r, r, &p);
    pa_fill_ellipse(c, x - r * 0.38f, y - r * 0.40f, r * 0.24f, r * 0.15f, pa_hex(0x8C8C94));
    pa_fill_circle(c, x - r * 0.44f, y - r * 0.44f, r * 0.08f, PA_RGB(230, 230, 236));
}

static void draw_balls(PA_Canvas *c, int cup_pass) {
    for (int i = 0; i < g_nb; i++) {
        Ball *b = &g_b[i];
        if (!b->alive || b->kind == BK_BOMB) continue;
        int inside = b->y > g_rim - BR && fabsf(b->x - g_cx) < g_hw + BR;
        if (inside != cup_pass) continue;
        PA_Color col = b->kind == BK_GREY ? GREY_COL : g_pal[b->col];
        draw_ball(c, sx(b->x), sy(b->y), b->r * g_s, col, b->flash);
    }
}

/* A pin: a thin blue rod through the glass, a ring on the outside end. */
/* A pin: a chunky blue cylinder through the glass with a fat ring handle on
   the outside end. */
static void draw_pin(PA_Canvas *c, const Pin *p) {
    if (p->state == 2) return;
    float a = 1.0f;
    if (p->state == 1) a = 1.0f - pa_clamp01((p->t - 0.6f) / 0.4f);
    float ax, ay, bx, by;
    pin_points(p, &ax, &ay, &bx, &by);
    if (g_tug > 0 && p == &g_pin[g_hand_target < 0 ? 0 : g_hand_target] && g_hand_target >= 0) {
        ax += p->dx * g_tug; ay += p->dy * g_tug; bx += p->dx * g_tug; by += p->dy * g_tug;
    }
    float x0 = sx(ax), y0 = sy(ay), x1 = sx(bx), y1 = sy(by);
    float w = PIN_R * 2.0f * g_s;
    PA_Color base = pa_alpha(PIN_COL, a), dark = pa_alpha(pa_shade(PIN_COL, -0.42f), a);
    PA_Color lite = pa_alpha(pa_hex(0x7DB8FF), a), shadow = PA_RGBA(60, 64, 80, (int)(46.0f * a));
    float rr = RING_R * g_s, rw = RING_W * g_s;
    float rcx = x1 + p->dx * rr, rcy = y1 + p->dy * rr;
    float so = (float)g_cw * 0.008f;
    pa_line(c, x0 + so * 0.6f, y0 + so, x1 + so * 0.6f, y1 + so, w, shadow);
    pa_stroke_circle(c, rcx + so * 0.6f, rcy + so, rr, rw, shadow);
    pa_line(c, x0, y0, x1, y1, w + 2.0f, dark);
    pa_fill_circle(c, x0, y0, (w + 2.0f) * 0.5f, dark);
    pa_line(c, x0, y0, x1, y1, w - 0.4f, base);
    pa_fill_circle(c, x0, y0, (w - 0.4f) * 0.5f, base);
    float nx = p->dy, ny = -p->dx;
    if (ny > 0 || (ny == 0 && nx > 0)) { nx = -nx; ny = -ny; }
    float hx = nx * w * 0.20f, hy = ny * w * 0.20f;
    pa_line(c, x0 + hx, y0 + hy, x1 + hx, y1 + hy, w * 0.30f, lite);
    /* ring: dark rim, blue body, a light arc on its upper-left */
    pa_stroke_circle(c, rcx, rcy, rr, rw + 2.0f, dark);
    pa_stroke_circle(c, rcx, rcy, rr, rw - 0.4f, base);
    PA_Vec2 arc[24];
    for (int k = 0; k < 24; k++) {
        float t = PA_PI * 0.95f + PA_PI * 0.75f * (float)k / 23.0f;
        arc[k].x = rcx + cosf(t) * (rr - rw * 0.08f); arc[k].y = rcy + sinf(t) * (rr - rw * 0.08f);
    }
    pa_stroke_poly(c, arc, 24, 0, rw * 0.30f, lite);
}

static PA_Color cup_colour(void) {
    static const uint32_t rims[6] = { 0xFF7A1A, 0x58D63A, 0x2E9BF0, 0xFFC21F, 0x1FD3B6, 0xF25CB4 };
    return pa_hex(rims[g_layout % 6]);
}

static void rim_half(PA_Canvas *c, float cx, float cy, float rx, float ry, float thick, PA_Color col, int front) {
    PA_Vec2 pts[40];
    int n = 0;
    for (int k = 0; k <= 32; k++) {
        float a = front ? PA_PI * (float)k / 32.0f : PA_PI + PA_PI * (float)k / 32.0f;
        pts[n].x = cx + cosf(a) * rx; pts[n].y = cy + sinf(a) * ry; n++;
    }
    /* a torus seen from above: darker underside, bright crown */
    PA_Vec2 q[40];
    for (int k = 0; k < n; k++) { q[k].x = pts[k].x; q[k].y = pts[k].y + thick * 0.22f; }
    pa_stroke_poly(c, q, n, 0, thick + 2.0f, pa_shade(col, -0.45f));
    pa_stroke_poly(c, pts, n, 0, thick, col);
    for (int k = 0; k < n; k++) { q[k].x = pts[k].x; q[k].y = pts[k].y - thick * 0.20f; }
    if (front) pa_stroke_poly(c, q + 4, n - 8, 0, thick * 0.34f, pa_shade(col, 0.40f));
    else pa_stroke_poly(c, q + 6, n - 12, 0, thick * 0.26f, pa_shade(col, 0.25f));
}

static void draw_cup_back(PA_Canvas *c) {
    float cx = sx(g_cx), top = sy(g_rim), bot = sy(g_bot + 4.0f);
    float hw = (g_hw + 4.0f) * g_s, ey = 9.0f * g_s;
    pa_shadow(c, cx, bot + 2.0f * g_s, hw * 1.25f, 12.0f * g_s, 0.20f);
    /* glass body, back face */
    PA_Paint p = pa_linear(cx - hw, 0, cx + hw, 0);
    pa_stop(&p, 0.0f, PA_RGBA(255, 255, 255, 110));
    pa_stop(&p, 0.25f, PA_RGBA(255, 255, 255, 55));
    pa_stop(&p, 0.75f, PA_RGBA(245, 246, 250, 50));
    pa_stop(&p, 1.0f, PA_RGBA(255, 255, 255, 110));
    pa_fill_rect_paint(c, cx - hw, top, hw * 2.0f, bot - top, &p);
    pa_fill_ellipse_paint(c, cx, bot, hw, ey, &p);
    pa_fill_ellipse(c, cx, bot - 2.0f * g_s, hw * 0.92f, ey * 0.8f, PA_RGBA(150, 154, 162, 40));
    /* the opening glows faintly in the rim colour */
    PA_Paint gl = pa_radial(cx, top, 0, hw + 6.0f * g_s);
    pa_stop(&gl, 0.0f, pa_alpha(cup_colour(), 0.10f));
    pa_stop(&gl, 0.75f, pa_alpha(cup_colour(), 0.25f));
    pa_stop(&gl, 1.0f, pa_alpha(cup_colour(), 0.34f));
    pa_fill_ellipse_paint(c, cx, top, hw + 5.0f * g_s, ey + 3.0f * g_s, &gl);
    rim_half(c, cx, top, hw + 6.0f * g_s, ey + 3.0f * g_s, 15.0f * g_s, cup_colour(), 0);
}

static void draw_cup_front(PA_Canvas *c) {
    float cx = sx(g_cx), top = sy(g_rim), bot = sy(g_bot + 4.0f);
    float hw = (g_hw + 4.0f) * g_s, ey = 9.0f * g_s;
    float b = g_cup_bounce * 1.5f * g_s;
    /* glass front: a faint sheen, thin side walls and the base curve */
    pa_fill_rect(c, cx - hw, top, hw * 2.0f, bot - top, PA_RGBA(255, 255, 255, 34));
    pa_line(c, cx - hw, top, cx - hw, bot, 2.0f * g_s, PA_RGBA(255, 255, 255, 190));
    pa_line(c, cx + hw, top, cx + hw, bot, 2.0f * g_s, PA_RGBA(200, 204, 210, 200));
    PA_Vec2 base[20];
    for (int k = 0; k < 20; k++) {
        float a = PA_PI * (float)k / 19.0f;
        base[k].x = cx + cosf(a) * hw; base[k].y = bot + sinf(a) * ey;
    }
    pa_stroke_poly(c, base, 20, 0, 2.0f * g_s, PA_RGBA(220, 222, 228, 220));
    /* the long vertical highlight the reference cup carries */
    PA_Paint hl = pa_linear(0, top, 0, bot);
    pa_stop(&hl, 0.0f, PA_RGBA(255, 255, 255, 0));
    pa_stop(&hl, 0.25f, PA_RGBA(255, 255, 255, 220));
    pa_stop(&hl, 0.75f, PA_RGBA(255, 255, 255, 220));
    pa_stop(&hl, 1.0f, PA_RGBA(255, 255, 255, 0));
    pa_fill_rect_paint(c, cx - hw * 0.18f, top + 6.0f * g_s, 4.5f * g_s, bot - top - 8.0f * g_s, &hl);
    pa_fill_rect_paint(c, cx - hw * 0.62f, top + 10.0f * g_s, 2.0f * g_s, bot - top - 20.0f * g_s, &hl);
    float pulse = g_cup_pulse * 1.5f * g_s;
    rim_half(c, cx, top - b * 0.5f, hw + 6.0f * g_s + pulse, ey + 3.0f * g_s, 15.0f * g_s + pulse * 0.5f, cup_colour(), 1);

    /* percentage pill under the cup */
    char buf[16];
    int pct = (int)(g_shown_pct + 0.5f);
    if (pct > 100) pct = 100;
    snprintf(buf, sizeof buf, "%d%%", pct);
    float pw = 76.0f * g_s, ph = 30.0f * g_s;
    float px = cx - pw * 0.5f, py = sy(g_bot + 38.0f);
    if (py + ph > (float)g_ch - 6.0f) py = (float)g_ch - 6.0f - ph;
    PA_Color pill = pct >= 100 ? pa_hex(0x3FC45A) : PA_RGBA(120, 122, 126, 190);
    pa_round_rect(c, px, py, pw, ph, ph * 0.32f, pill);
    float ts = 17.0f * g_s;
    pa_text(c, buf, cx, py + ph * 0.5f - ts * 0.5f, ts, PA_RGBA(235, 236, 238, 255), PA_ALIGN_CENTER, 1.0f);
}

/* screen: 1 screen-space particles, 0 design-space ones except the blast,
   2 the blast cloud alone (drawn behind the balls so scorched ones show) */
static void draw_particles(PA_Canvas *c, int screen) {
    for (int i = 0; i < g_npart; i++) {
        Part *p = &g_part[i];
        if (screen == 2) { if (p->screen || p->kind != 4) continue; }
        else if (p->screen != screen || (!screen && p->kind == 4)) continue;
        float a = pa_clamp01(p->life / p->max * 2.0f);
        float x = screen == 1 ? p->x : sx(p->x), y = screen == 1 ? p->y : sy(p->y);
        float k = screen == 1 ? 1.0f : g_s;
        if (p->kind == 0) {
            float cw = p->w * 0.5f * k, ch = p->h * 0.5f * k * (0.3f + 0.7f * fabsf(sinf(p->rot * 0.7f)));
            float cs = cosf(p->rot), sn = sinf(p->rot);
            PA_Vec2 q[4] = {
                { x - cs * cw + sn * ch, y - sn * cw - cs * ch }, { x + cs * cw + sn * ch, y + sn * cw - cs * ch },
                { x + cs * cw - sn * ch, y + sn * cw + cs * ch }, { x - cs * cw - sn * ch, y - sn * cw + cs * ch }
            };
            pa_fill_poly(c, q, 4, pa_alpha(p->col, a));
        } else if (p->kind == 1) {
            pa_fill_circle(c, x, y, p->w * k, pa_alpha(p->col, a));
        } else if (p->kind == 2) {
            pa_fill_circle(c, x, y, p->w * k, pa_alpha(p->col, a * 0.8f));
        } else if (p->kind == 4) {
            pa_fill_circle(c, x, y, p->w * k, pa_alpha(p->col, 0.80f * pa_clamp01(p->life / (p->max - 0.4f))));
        } else {
            float t = 1.0f - p->life / p->max;
            pa_stroke_circle(c, x, y, p->w * k * (1.0f + t * 1.2f), 1.8f * k * (1.0f - t) + 0.4f, pa_alpha(p->col, 1.0f - t));
        }
    }
}

/* ---- the cartoon hand: pale glove-like hand, white cuff, finger to the tip */
static void xf_poly(PA_Canvas *c, const PA_Vec2 *loc, int n, float x, float y, float s, float ang, PA_Color col) {
    PA_Vec2 pts[64];
    float cs = cosf(ang), sn = sinf(ang);
    for (int k = 0; k < n && k < 64; k++) {
        float lx = loc[k].x * s, ly = loc[k].y * s;
        pts[k].x = x + lx * cs - ly * sn; pts[k].y = y + lx * sn + ly * cs;
    }
    pa_fill_poly(c, pts, n < 64 ? n : 64, col);
}

static int rrect_pts(PA_Vec2 *o, float x, float y, float w, float h, float r) {
    int n = 0;
    float cxs[4] = { x + w - r, x + r, x + r, x + w - r }, cys[4] = { y + h - r, y + h - r, y + r, y + r };
    for (int q = 0; q < 4; q++)
        for (int k = 0; k <= 4; k++) {
            float a = (float)q * PA_PI * 0.5f + PA_PI * 0.5f * (float)k / 4.0f;
            o[n].x = cxs[q] + cosf(a) * r; o[n].y = cys[q] + sinf(a) * r; n++;
        }
    return n;
}

/* One rounded part of the hand, shaded: an outline, then a fill lit from the
   upper-left of the part and falling off to its lower-right. */
static void hand_part(PA_Canvas *c, float x, float y, float w, float h, float r, float hx, float hy,
                      float s, float ang, PA_Color fill, PA_Color edge) {
    PA_Vec2 pts[24], q[24];
    int n = rrect_pts(pts, x - 1.4f, y - 1.4f, w + 2.8f, h + 2.8f, r + 1.4f);
    xf_poly(c, pts, n, hx, hy, s, ang, edge);
    n = rrect_pts(pts, x, y, w, h, r);
    float cs = cosf(ang), sn = sinf(ang);
    for (int k = 0; k < n; k++) {
        float lx = pts[k].x * s, ly = pts[k].y * s;
        q[k].x = hx + lx * cs - ly * sn; q[k].y = hy + lx * sn + ly * cs;
    }
    float ax = x * s, ay = y * s, bx = (x + w) * s, by = (y + h) * s;
    PA_Paint p = pa_linear(hx + ax * cs - ay * sn, hy + ax * sn + ay * cs, hx + bx * cs - by * sn, hy + bx * sn + by * cs);
    pa_stop(&p, 0.0f, pa_shade(fill, 0.35f));
    pa_stop(&p, 0.45f, fill);
    pa_stop(&p, 1.0f, pa_shade(fill, -0.10f));
    pa_fill_poly_paint(c, q, n, &p);
}

static void draw_hand(PA_Canvas *c, float x, float y, float press, float s) {
    /* local frame: fingertip at the origin, finger up, the arm down-right.
       The pose leans further upright (then past it) until the whole hand,
       cuff and all, sits on screen. */
    static const float angs[7] = { -0.58f, -0.42f, -0.26f, -0.10f, 0.08f, 0.26f, 0.44f };
    float lift = (1.0f - press);
    s *= 1.0f - 0.05f * press;
    x += 10.0f * lift * s; y += 16.0f * lift * s;
    float ang = angs[0];
    for (int i = 0; i < 7; i++) {
        float cs = cosf(angs[i]), sn = sinf(angs[i]);
        float lo = 1e9f, hi = -1e9f, bot = -1e9f;
        static const float cx[4] = { -32, 44, -32, 44 }, cy[4] = { 0, 0, 124, 124 };
        for (int k = 0; k < 4; k++) {
            float px = x + (cx[k] * cs - cy[k] * sn) * s, py = y + (cx[k] * sn + cy[k] * cs) * s;
            if (px < lo) lo = px;
            if (px > hi) hi = px;
            if (py > bot) bot = py;
        }
        ang = angs[i];
        if (lo >= 4.0f && hi <= (float)c->w - 4.0f && bot <= (float)c->h - 4.0f) break;
    }
    PA_Color skin = pa_hex(0xFFE2D2), edge = pa_hex(0xD9A894), crease = pa_hex(0xEDBFAA);
    float cs = cosf(ang), sn = sinf(ang);
    pa_shadow(c, x + (20.0f * cs - 110.0f * sn) * s + 14.0f * s, y + (20.0f * sn + 110.0f * cs) * s + 18.0f * s,
              52.0f * s, 22.0f * s, 0.12f + 0.06f * press);
    hand_part(c, -24, 92, 66, 30, 9, x, y, s, ang, pa_hex(0xFFFFFF), pa_hex(0xA9B7CC));   /* cuff */
    hand_part(c, -24, 112, 66, 11, 4, x, y, s, ang, pa_hex(0xC9D6EA), pa_hex(0xA9B7CC));
    hand_part(c, -30, 52, 20, 36, 10, x, y, s, ang, skin, edge);                         /* thumb */
    hand_part(c, -20, 40, 58, 58, 20, x, y, s, ang, skin, edge);                         /* palm */
    hand_part(c, 16, 44, 26, 17, 8.5f, x, y, s, ang, skin, edge);                        /* curled fingers */
    hand_part(c, 17, 60, 25, 17, 8.5f, x, y, s, ang, skin, edge);
    hand_part(c, 16, 76, 23, 15, 7.5f, x, y, s, ang, skin, edge);
    hand_part(c, -9, 0, 18, 62, 9, x, y, s, ang, skin, edge);                            /* index */
    PA_Vec2 ln[24];
    int n = rrect_pts(ln, -5, 3, 10, 11, 5);
    xf_poly(c, ln, n, x, y, s, ang, pa_hex(0xFFF1EA));                                   /* nail */
    n = rrect_pts(ln, -6, 32, 12, 2.6f, 1.3f);
    xf_poly(c, ln, n, x, y, s, ang, crease);                                             /* knuckle crease */
    n = rrect_pts(ln, -14, 48, 10, 30, 5);
    xf_poly(c, ln, n, x, y, s, ang, PA_RGBA(255, 255, 255, 90));                         /* palm sheen */
}

/* ---- the collection picture: a still life of a full cup, in 12 pieces ---- */
static void draw_picture(PA_Canvas *c, float x, float y, float w, float h, int pic) {
    static const uint32_t skies[4][2] = { { 0x7FD3FF, 0xFFE3F1 }, { 0xFFB36B, 0xFFE9A8 }, { 0x9C8CFF, 0xFFC8E8 }, { 0x6BE3C0, 0xE8FFF4 } };
    const uint32_t *sk = skies[pic & 3];
    PA_Paint p = pa_linear(0, y, 0, y + h);
    pa_stop(&p, 0.0f, pa_hex(sk[0]));
    pa_stop(&p, 1.0f, pa_hex(sk[1]));
    pa_fill_rect_paint(c, x, y, w, h, &p);
    pa_fill_circle(c, x + w * 0.78f, y + h * 0.22f, h * 0.13f, PA_RGBA(255, 255, 255, 170));
    pa_fill_ellipse(c, x + w * 0.5f, y + h * 0.96f, w * 0.7f, h * 0.18f, PA_RGBA(255, 255, 255, 120));
    float cx = x + w * 0.5f, cw = w * 0.20f, ct = y + h * 0.42f, cb = y + h * 0.86f;
    PA_Rng r;
    pa_rng_seed(&r, 7u + (uint32_t)pic);
    for (int i = 0; i < 90; i++) {
        float bx = cx + pa_rng_range(&r, -cw * 0.9f, cw * 0.9f);
        float by = cb - pa_rng_range(&r, 0, (cb - ct) * 1.25f);
        if (by < ct) { float dx = (bx - cx) / cw; if (by < ct - (1.0f - dx * dx) * h * 0.12f) continue; }
        draw_ball(c, bx, by, w * 0.026f, pa_hex(SKINS[pic % 6][pa_rng_int(&r, 0, 7)]), 0);
    }
    pa_fill_rect(c, cx - cw, ct, cw * 2.0f, cb - ct, PA_RGBA(255, 255, 255, 60));
    pa_fill_rect(c, cx - cw * 0.55f, ct + 6.0f, w * 0.018f, cb - ct - 12.0f, PA_RGBA(255, 255, 255, 170));
    rim_half(c, cx, ct, cw + w * 0.02f, h * 0.035f, w * 0.035f, pa_hex(0xFF7A1A), 1);
}

static void panel_title(PA_Canvas *c, Box b, const char *t, PA_Color band) {
    pa_round_rect(c, b.x, b.y, b.w, 78.0f, 24.0f, band);
    pa_fill_rect(c, b.x, b.y + 50.0f, b.w, 28.0f, band);
    PA_TextStyle st = pa_text_style(PA_FACE_DISPLAY, PA_RGB(255, 255, 255));
    st.align = PA_ALIGN_CENTER; st.outline = 3.0f; st.outline_col = pa_shade(band, -0.45f);
    st.shadow_dy = 3.0f; st.shadow_col = pa_shade(band, -0.45f);
    pa_text_ex(c, t, b.x + b.w * 0.5f, b.y + 24.0f, 30.0f, &st);
}

static void draw_panel(PA_Canvas *c) {
    if (g_panel == PANEL_NONE) return;
    pa_hub_hide_pause();
    float k = pa_smooth(pa_clamp01(g_panel_t * 5.0f));
    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(30, 32, 44, (int)(150.0f * k)));
    Box b = panel_card();
    b.y += (1.0f - k) * 60.0f;
    pa_round_rect(c, b.x, b.y + 8.0f, b.w, b.h, 24.0f, PA_RGBA(20, 22, 36, 70));
    pa_round_rect(c, b.x, b.y, b.w, b.h, 24.0f, pa_hex(0xF4F5F8));
    char buf[48];
    if (g_panel == PANEL_COLLECTION) {
        panel_title(c, b, "COLLECTION", pa_hex(0x2E9BF0));
        int done = cleared(), pic = done / 12, pieces = done % 12;
        snprintf(buf, sizeof buf, "PICTURE %d  -  %d/12 PIECES", pic + 1, pieces);
        pa_text(c, buf, b.x + b.w * 0.5f, b.y + 98.0f, 15.0f, INK, PA_ALIGN_CENTER, 1.0f);
        float gx = b.x + 24.0f, gw = b.w - 48.0f, gy = b.y + 130.0f, gh = gw * 1.05f;
        if (gh > b.h - 220.0f) gh = b.h - 220.0f;
        float tw = gw / 3.0f, th = gh / 4.0f;
        for (int i = 0; i < 12; i++) {
            float tx = gx + (float)(i % 3) * tw, ty = gy + (float)(i / 3) * th;
            if (i < pieces) {
                pa_clip_rect(c, (int)(tx + 2), (int)(ty + 2), (int)(tw - 4), (int)(th - 4));
                draw_picture(c, gx, gy, gw, gh, pic);
                pa_clip_reset(c);
            } else {
                pa_round_rect(c, tx + 3, ty + 3, tw - 6, th - 6, 10.0f, pa_hex(0xDADDE4));
                pa_text_bold(c, "?", tx + tw * 0.5f, ty + th * 0.5f - 14.0f, 28.0f, pa_hex(0xB4B9C4), pa_hex(0xB4B9C4), PA_ALIGN_CENTER, 0, 0.2f);
            }
        }
        pa_text(c, "EVERY LEVEL CLEARED ADDS A PIECE", b.x + b.w * 0.5f, gy + gh + 22.0f, 13.0f, pa_hex(0x6A707C), PA_ALIGN_CENTER, 1.0f);
    } else if (g_panel == PANEL_ACHIEVE) {
        panel_title(c, b, "ACHIEVEMENTS", pa_hex(0xFFA41F));
        float rh = (b.h - 120.0f) / (float)ACH_COUNT;
        for (int a = 0; a < ACH_COUNT; a++) {
            float ry = b.y + 96.0f + (float)a * rh;
            int v = ach_value(a), g = ACH_GOAL[a], ok = v >= g;
            pa_round_rect(c, b.x + 16.0f, ry, b.w - 32.0f, rh - 10.0f, 16.0f, PA_RGB(255, 255, 255));
            float mx2 = b.x + 52.0f, my = ry + (rh - 10.0f) * 0.5f;
            pa_fill_circle(c, mx2, my, 22.0f, ok ? pa_hex(0xFFC21F) : pa_hex(0xD5D8DF));
            pa_fill_circle(c, mx2, my, 15.0f, ok ? pa_hex(0xFFE07A) : pa_hex(0xE8EAEF));
            pa_text_bold(c, ok ? "!" : "?", mx2, my - 9.0f, 18.0f, ok ? pa_hex(0xB06A00) : pa_hex(0xA0A6B2), ok ? pa_hex(0xB06A00) : pa_hex(0xA0A6B2), PA_ALIGN_CENTER, 0, 0.2f);
            pa_text_bold(c, ACH_NAMES[a], b.x + 86.0f, ry + 10.0f, 17.0f, INK, INK, PA_ALIGN_LEFT, 1.0f, 0.1f);
            pa_text(c, ACH_DESC[a], b.x + 86.0f, ry + 33.0f, 11.0f, pa_hex(0x6A707C), PA_ALIGN_LEFT, 0.8f);
            float bw = b.w - 200.0f, bx = b.x + 86.0f, by = ry + rh - 30.0f;
            pa_round_rect(c, bx, by, bw, 10.0f, 5.0f, pa_hex(0xE3E6EC));
            float f = pa_clamp01((float)v / (float)g);
            if (f > 0.02f) pa_round_rect(c, bx, by, bw * f, 10.0f, 5.0f, ok ? pa_hex(0x3FC45A) : pa_hex(0x2E9BF0));
            snprintf(buf, sizeof buf, "%d/%d", v < g ? v : g, g);
            pa_text(c, buf, b.x + b.w - 30.0f, by - 3.0f, 12.0f, INK, PA_ALIGN_RIGHT, 0.5f);
        }
    } else {
        panel_title(c, b, "BALL SKINS", pa_hex(0xF25CB4));
        int sel = pa_save_get("pins.skin", 0);
        if (g_demo) sel = 0;
        for (int s2 = 0; s2 < 6; s2++) {
            Box t = skin_tile(s2);
            int open = cleared() >= SKIN_UNLOCK[s2] || s2 == 0;
            pa_round_rect(c, t.x, t.y, t.w, t.h, 18.0f, s2 == sel ? pa_hex(0x3FC45A) : PA_RGB(255, 255, 255));
            pa_round_rect(c, t.x + 4, t.y + 4, t.w - 8, t.h - 8, 15.0f, PA_RGB(255, 255, 255));
            float r = t.w * 0.07f;
            for (int i = 0; i < 8; i++) {
                float bx = t.x + t.w * 0.5f + (float)(i % 4 - 1.5f) * r * 2.3f + (i >= 4 ? r * 1.1f : 0.0f);
                float by = t.y + t.h * 0.42f - (i >= 4 ? r * 1.9f : 0.0f);
                draw_ball(c, bx, by, r, open ? pa_hex(SKINS[s2][i]) : pa_hex(0xC4C8D0), 0);
            }
            pa_text_bold(c, SKIN_NAMES[s2], t.x + t.w * 0.5f, t.y + t.h * 0.66f, 15.0f, INK, INK, PA_ALIGN_CENTER, 1.0f, 0.1f);
            if (!open) {
                snprintf(buf, sizeof buf, "CLEAR %d LEVELS", SKIN_UNLOCK[s2]);
                pa_text(c, buf, t.x + t.w * 0.5f, t.y + t.h * 0.82f, 11.0f, pa_hex(0x8A909C), PA_ALIGN_CENTER, 0.5f);
            } else if (s2 == sel) {
                pa_text(c, "IN USE", t.x + t.w * 0.5f, t.y + t.h * 0.82f, 11.0f, pa_hex(0x2E9A48), PA_ALIGN_CENTER, 0.8f);
            }
        }
    }
    Box x = panel_close();
    x.y += (1.0f - k) * 60.0f;
    float cx = x.x + x.w * 0.5f, cy = x.y + x.h * 0.5f;
    pa_fill_circle(c, cx, cy + 3.0f, 22.0f, PA_RGBA(20, 22, 36, 60));
    pa_fill_circle(c, cx, cy, 22.0f, pa_hex(0xE8442E));
    pa_line(c, cx - 8, cy - 8, cx + 8, cy + 8, 4.5f, PA_RGB(255, 255, 255));
    pa_line(c, cx + 8, cy - 8, cx - 8, cy + 8, 4.5f, PA_RGB(255, 255, 255));
}


static void news_dot(PA_Canvas *c, float x, float y) {
    pa_fill_circle(c, x, y, 8.5f, PA_RGB(255, 255, 255));
    pa_fill_circle(c, x, y, 7.0f, pa_hex(0xE53935));
    pa_text_bold(c, "!", x, y - 5.0f, 10.0f, PA_RGB(255, 255, 255), PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 0, 0.2f);
}

/* outline-and-fill helper for the chunky icon set: the ink silhouette a few
   px fatter, then the fill on top */
static void icon_rrect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color fill) {
    pa_round_rect(c, x - 3.2f, y - 3.2f, w + 6.4f, h + 6.4f, r + 3.2f, INK);
    pa_round_rect(c, x, y, w, h, r, fill);
}

static void draw_hud(PA_Canvas *c) {
    float x0, x1;
    hud_span(&x0, &x1);
    pa_round_rect(c, x0, -24.0f + 5.0f, x1 - x0, HUD_H + 24.0f, 16.0f, PA_RGBA(40, 44, 60, 40));
    pa_round_rect(c, x0, -24.0f, x1 - x0, HUD_H + 24.0f, 16.0f, PA_RGB(255, 255, 255));
    float cy = HUD_H * 0.5f + 4.0f;
    char buf[16];
    /* level badge: number in a ring, LVL under it */
    float lx = hud_icon_x(HUD_LEVEL);
    pa_stroke_circle(c, lx, cy - 3.0f, 19.0f, 3.8f, INK);
    snprintf(buf, sizeof buf, "%d", g_level + 1);
    float ts = g_level + 1 >= 100 ? 13.0f : 17.0f;
    pa_text_bold(c, buf, lx, cy - 3.0f - ts * 0.5f, ts, INK, INK, PA_ALIGN_CENTER, 0.5f, 0.2f);
    pa_round_rect(c, lx - 17.0f, cy + 11.0f, 34.0f, 13.0f, 5.0f, PA_RGB(255, 255, 255));
    pa_text_bold(c, "LVL", lx, cy + 13.0f, 9.0f, INK, INK, PA_ALIGN_CENTER, 0.6f, 0.1f);
    /* collection: a puzzle piece */
    {
        float x = hud_icon_x(HUD_COLLECTION);
        for (int pass = 0; pass < 2; pass++) {
            float g = pass ? 0.0f : 3.2f;
            PA_Color col = pass ? pa_hex(0x58C7FF) : INK;
            pa_round_rect(c, x - 13.0f - g, cy - 9.0f - g, 24.0f + 2 * g, 24.0f + 2 * g, 4.0f + g, col);
            pa_fill_circle(c, x - 1.0f, cy - 11.0f, 6.0f + g, col);
            pa_fill_circle(c, x + 13.0f, cy + 3.0f, 6.0f + g, col);
        }
        pa_fill_circle(c, x - 6.0f, cy - 3.0f, 3.0f, PA_RGBA(255, 255, 255, 170));
        if (has_news(PANEL_COLLECTION)) news_dot(c, x + 16.0f, cy - 16.0f);
    }
    /* achievements: a trophy */
    {
        float x = hud_icon_x(HUD_ACHIEVE);
        for (int pass = 0; pass < 2; pass++) {
            float g = pass ? 0.0f : 3.2f;
            PA_Color col = pass ? pa_hex(0xFFC21F) : INK;
            PA_Vec2 bowl[8] = { { x - 13 - g, cy - 15 - g }, { x + 13 + g, cy - 15 - g }, { x + 12 + g, cy - 4 },
                                { x + 6 + g * 0.6f, cy + 4 + g * 0.4f }, { x - 6 - g * 0.6f, cy + 4 + g * 0.4f }, { x - 12 - g, cy - 4 } };
            pa_fill_poly(c, bowl, 6, col);
            pa_round_rect(c, x - 3.0f - g, cy + 2.0f, 6.0f + 2 * g, 8.0f, 1.0f, col);
            pa_round_rect(c, x - 10.0f - g, cy + 9.0f - g, 20.0f + 2 * g, 7.0f + 2 * g, 3.0f + g, col);
            if (!pass) {
                pa_stroke_circle(c, x - 13.0f, cy - 8.0f, 6.0f, 3.6f, INK);
                pa_stroke_circle(c, x + 13.0f, cy - 8.0f, 6.0f, 3.6f, INK);
            }
        }
        pa_fill_rect(c, x - 7.0f, cy - 12.0f, 3.0f, 9.0f, PA_RGBA(255, 255, 255, 180));
        if (has_news(PANEL_ACHIEVE)) news_dot(c, x + 16.0f, cy - 16.0f);
    }
    /* hint: the yellow bulb */
    {
        float x = hud_icon_x(HUD_HINT);
        pa_fill_circle(c, x, cy - 5.0f, 15.0f, INK);
        pa_fill_circle(c, x, cy - 5.0f, 11.8f, pa_hex(0xFFD43B));
        icon_rrect(c, x - 6.5f, cy + 7.0f, 13.0f, 9.0f, 2.5f, pa_hex(0xC9CED8));
        pa_fill_circle(c, x - 4.5f, cy - 9.0f, 3.2f, PA_RGBA(255, 255, 255, 220));
    }
    /* skins: a paint brush with a wet tip */
    {
        float x = hud_icon_x(HUD_SKINS);
        pa_line(c, x + 12.0f, cy + 14.0f, x - 2.0f, cy - 1.0f, 9.0f, INK);
        pa_line(c, x + 12.0f, cy + 14.0f, x - 2.0f, cy - 1.0f, 3.2f, pa_hex(0xC98B4E));
        PA_Vec2 head[4] = { { x - 2.0f, cy - 6.0f }, { x + 4.0f, cy }, { x - 6.0f, cy + 9.0f }, { x - 15.0f, cy + 4.0f } };
        PA_Vec2 hd2[4] = { { x - 2.5f, cy - 10.5f }, { x + 8.5f, cy + 0.5f }, { x - 6.5f, cy + 13.5f }, { x - 19.5f, cy + 6.0f } };
        pa_fill_poly(c, hd2, 4, INK);
        pa_fill_poly(c, head, 4, pa_hex(0xF25CB4));
        pa_fill_circle(c, x - 14.0f, cy + 11.0f, 4.0f, INK);
        pa_fill_circle(c, x - 14.0f, cy + 11.0f, 2.2f, pa_hex(0x2E9BF0));
        if (has_news(PANEL_SKINS)) news_dot(c, x + 16.0f, cy - 16.0f);
    }
    pa_hub_pause_anchor(hud_icon_x(HUD_PAUSE), cy, 19.0f);
    pa_stroke_circle(c, hud_icon_x(HUD_PAUSE), cy, 22.0f, 3.8f, INK);
}

static void draw_floor_buttons(PA_Canvas *c) {
    for (int k = 0; k < 2; k++) {
        Box b = floor_btn(k);
        float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, r = b.w * 0.5f;
        pa_shadow(c, cx, cy + r * 0.9f, r * 0.9f, r * 0.25f, 0.18f);
        pa_fill_circle(c, cx, cy + 3.0f, r, pa_hex(0xC4C7CF));
        pa_fill_circle(c, cx, cy, r, PA_RGB(255, 255, 255));
        if (k == 0) {
            PA_Vec2 pts[40];
            int n = 0;
            float rr = r * 0.45f;
            for (int i = 0; i <= 30; i++) {
                float a = -PA_PI * 0.30f + (PA_TAU * 0.80f) * (float)i / 30.0f;
                pts[n].x = cx + cosf(a) * rr; pts[n].y = cy + sinf(a) * rr; n++;
            }
            pa_stroke_poly(c, pts, n, 0, 5.0f, INK);
            float a0 = -PA_PI * 0.30f, ex = cx + cosf(a0) * rr, ey = cy + sinf(a0) * rr;
            PA_Vec2 tri[3] = { { ex - 8.0f, ey - 3.0f }, { ex + 7.0f, ey - 9.0f }, { ex + 4.5f, ey + 8.0f } };
            pa_fill_poly(c, tri, 3, INK);
        } else {
            for (int i = 0; i < 2; i++) {
                float o = i ? 6.0f : -7.0f;
                PA_Vec2 tri[3] = { { cx + o - 7.0f, cy - 10.0f }, { cx + o + 7.0f, cy }, { cx + o - 7.0f, cy + 10.0f } };
                pa_fill_poly(c, tri, 3, INK);
            }
        }
        pa_text_bold(c, k ? "SKIP" : "RETRY", cx, cy + r + 8.0f, 11.0f, INK, INK, PA_ALIGN_CENTER, 0.8f, 0.1f);
    }
}

static void button(PA_Canvas *c, Box b, const char *label, PA_Color col, float pop) {
    float k = 1.0f + pop;
    float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    float bw = b.w * k, bh = b.h * k;
    pa_round_rect(c, cx - bw * 0.5f, cy - bh * 0.5f + 7.0f, bw, bh, bh * 0.5f, pa_shade(col, -0.32f));
    pa_round_rect(c, cx - bw * 0.5f, cy - bh * 0.5f, bw, bh, bh * 0.5f, col);
    pa_round_rect(c, cx - bw * 0.5f + bh * 0.3f, cy - bh * 0.5f + bh * 0.1f, bw - bh * 0.6f, bh * 0.26f,
                  bh * 0.13f, pa_alpha(PA_RGB(255, 255, 255), 0.35f));
    float ts = bh * 0.40f;
    pa_text_bold(c, label, cx, cy - ts * 0.5f, ts, PA_RGB(255, 255, 255), pa_shade(col, -0.5f),
                 PA_ALIGN_CENTER, 2.0f, 1.4f);
}

static float ease_out_back(float t) {
    t = pa_clamp01(t);
    float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

/* the chunky title treatment: white face, orange outline, dark drop */
static void title(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color face, PA_Color line) {
    pa_text_bold(c, s, x, y + size * 0.10f, size, pa_shade(line, -0.45f), pa_shade(line, -0.45f), PA_ALIGN_CENTER, size * 0.06f, 2.6f);
    pa_text_bold(c, s, x, y, size, face, line, PA_ALIGN_CENTER, size * 0.06f, 2.2f);
}

static void draw_card(PA_Canvas *c) {
    float w = (float)c->w, h = (float)c->h;
    float t = g_state_t - (g_state == ST_WIN ? 0.30f : 0.55f);
    if (t < 0) return;
    pa_hub_hide_pause();
    float k = pa_clamp01(t * 4.0f);
    pa_fill_rect(c, 0, 0, w, h, PA_RGBA(238, 238, 240, (int)(212.0f * k)));
    float s = ease_out_back(t * 2.6f);
    char buf[32];
    snprintf(buf, sizeof buf, "LEVEL %d", g_level + 1);
    float ty = h * 0.24f;
    if (g_state == ST_WIN) {
        title(c, buf, w * 0.5f, ty, 34.0f * s, PA_RGB(255, 255, 255), pa_hex(0x3B4050));
        title(c, "COMPLETED!", w * 0.5f, ty + 62.0f, 50.0f * s, PA_RGB(255, 255, 255), pa_hex(0xF26A1B));
        /* a full cup trophy: the level's cup brimming with balls */
        float bt = pa_clamp01((t - 0.25f) * 3.0f);
        float cs = ease_out_back(bt);
        if (cs > 0.02f) {
            float cx = w * 0.5f, cy = ty + 250.0f, cw = 70.0f * cs, chh = 84.0f * cs;
            pa_shadow(c, cx, cy + chh * 0.55f, cw * 1.2f, 10.0f * cs, 0.25f);
            pa_round_rect(c, cx - cw, cy - chh * 0.45f, cw * 2.0f, chh, 10.0f * cs, PA_RGBA(255, 255, 255, 120));
            PA_Rng r; pa_rng_seed(&r, 99u + (uint32_t)g_level);
            for (int i = 0; i < 70; i++) {
                float bx = cx + pa_rng_range(&r, -cw + 7.0f * cs, cw - 7.0f * cs);
                float by = cy + chh * 0.48f - 7.0f * cs - pa_rng_range(&r, 0, chh * 1.05f);
                if (by < cy - chh * 0.45f) {
                    float dx = fabsf(bx - cx) / cw;
                    if (by < cy - chh * 0.45f - (1.0f - dx * dx) * 26.0f * cs) continue;
                }
                draw_ball(c, bx, by, 7.0f * cs, BALL_COLS[pa_rng_int(&r, 0, 7)], 0);
            }
            pa_fill_rect(c, cx - cw * 0.3f, cy - chh * 0.3f, 5.0f * cs, chh * 0.7f, PA_RGBA(255, 255, 255, 150));
            rim_half(c, cx, cy - chh * 0.45f, cw + 4.0f * cs, 10.0f * cs, 12.0f * cs, cup_colour(), 1);
        }
        button(c, card_btn(), "NEXT", pa_hex(0x3FC45A), 0.03f * sinf(g_time * 5.0f));
    } else {
        const char *why = g_fail_reason == FAIL_GREY ? "A GREY BALL REACHED THE CUP"
                        : g_fail_reason == FAIL_BOMB ? "A BOMB FELL IN THE CUP"
                        : g_fail_reason == FAIL_BOOM ? "A BOMB WENT OFF" : "NOT ENOUGH BALLS";
        title(c, buf, w * 0.5f, ty, 34.0f * s, PA_RGB(255, 255, 255), pa_hex(0x3B4050));
        title(c, "FAILED", w * 0.5f, ty + 62.0f, 58.0f * s, PA_RGB(255, 255, 255), pa_hex(0xE8442E));
        pa_text(c, why, w * 0.5f, ty + 168.0f, 17.0f, pa_hex(0x4A4F5A), PA_ALIGN_CENTER, 1.5f);
        button(c, card_btn(), "RETRY", pa_hex(0xFF8A1F), 0.0f);
        Box b2 = card_btn2();
        pa_text(c, "SKIP LEVEL", b2.x + b2.w * 0.5f, b2.y + b2.h * 0.5f - 9.0f, 18.0f, pa_hex(0x6A707C),
                PA_ALIGN_CENTER, 2.0f);
    }
}

static void s_render(PA_Canvas *c) {
    resolve_layout(c);
    float shx = 0, shy = 0;
    if (g_shake > 0.001f) {
        shx = sinf(g_time * 91.0f) * g_shake * 12.0f;
        shy = cosf(g_time * 77.0f) * g_shake * 9.0f;
    }
    if (g_kick > 0) {
        float k = g_kick / 0.15f;
        shx += sinf(g_time * 157.0f) * 6.0f * k;
        shy += cosf(g_time * 131.0f) * 6.0f * k;
    }
    draw_background(c);
    g_ox += shx; g_oy += shy;
    for (int i = 0; i < g_npoly; i++) if (g_poly[i].fill) glass_fill(c, &g_poly[i]);
    for (int i = 0; i < g_npoly; i++) if (g_poly[i].fill != 2) tube_stroke(c, g_poly[i].p, g_poly[i].n, WALL_R * 2.0f, 1, 0);
    draw_particles(c, 2);
    draw_cup_back(c);
    draw_balls(c, 1);
    draw_cup_front(c);
    draw_balls(c, 0);
    for (int i = 0; i < g_nb; i++) if (g_b[i].alive && g_b[i].kind == BK_BOMB) draw_bomb(c, &g_b[i]);
    for (int i = 0; i < g_npoly; i++) if (g_poly[i].fill != 2) tube_stroke(c, g_poly[i].p, g_poly[i].n, WALL_R * 2.0f, 1, 1);
    for (int k = 0; k < g_npin; k++) draw_pin(c, &g_pin[k]);
    draw_particles(c, 0);
    if (g_hand_x < DW + 100.0f && g_state == ST_PLAY)
        draw_hand(c, sx(g_hand_x), sy(g_hand_y), g_hand_press, 0.87f * (float)c->w / 540.0f);
    g_ox -= shx; g_oy -= shy;

    if (g_state == ST_PLAY) draw_floor_buttons(c);
    draw_hud(c);
    if (g_intro < 1.0f) pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(240, 240, 242, (int)(255.0f * (1.0f - g_intro))));
    if (g_state != ST_PLAY) draw_card(c);
    draw_panel(c);
    draw_particles(c, 1);
}

static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint p = pa_linear(0, y, 0, y + h);
    pa_stop(&p, 0.0f, pa_hex(0xE2E2E2));
    pa_stop(&p, 0.75f, pa_hex(0xC4C4C4));
    pa_stop(&p, 0.80f, pa_hex(0xD6D6D6));
    pa_stop(&p, 1.0f, pa_hex(0xDCDCDC));
    pa_fill_rect_paint(c, x, y, w, h, &p);
    float tx0 = x + w * 0.24f, tx1 = x + w * 0.76f, top = y + h * 0.10f, mid = y + h * 0.36f;
    float sp = x + w * 0.5f;
    float br = w * 0.024f;
    float cycle = fmodf(t * 0.45f, 1.0f);
    int k = 0;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 9; col++, k++) {
            float bx = tx0 + br * 2.0f + (float)col * br * 2.05f + ((row & 1) ? br : 0);
            float by = mid - br * 1.6f - (float)row * br * 1.75f;
            if (cycle > 0.35f) {
                float f = (cycle - 0.35f) * (1.0f + (float)(k % 7) * 0.05f);
                by += f * f * h * 2.2f;
                bx = pa_lerpf(bx, sp + (bx - sp) * 0.12f, pa_clamp01(f * 3.2f));
            }
            if (by > y + h * 0.80f) by = y + h * 0.80f - (float)(k % 5) * br * 1.6f;
            draw_ball(c, bx, by, br, BALL_COLS[k % 8], 0);
        }
    PA_Vec2 tube[6] = { { sp - w * 0.07f, y + h * 0.64f }, { sp - w * 0.07f, y + h * 0.56f }, { tx0, y + h * 0.44f },
                        { tx0, top }, { tx1, top }, { tx1, y + h * 0.44f } };
    PA_Vec2 tube2[2] = { { tx1, y + h * 0.44f }, { sp + w * 0.07f, y + h * 0.56f } };
    for (int ps = 0; ps < 2; ps++) {
        tube_stroke(c, tube, 6, w * 0.05f, 0, ps);
        tube_stroke(c, tube2, 2, w * 0.05f, 0, ps);
    }
    PA_Vec2 tube3[2] = { { sp + w * 0.07f, y + h * 0.56f }, { sp + w * 0.07f, y + h * 0.64f } };
    tube_stroke(c, tube3, 2, w * 0.05f, 0, 0);
    tube_stroke(c, tube3, 2, w * 0.05f, 0, 1);
    pa_fill_rect(c, sp - w * 0.13f, y + h * 0.70f, w * 0.26f, h * 0.16f, PA_RGBA(255, 255, 255, 120));
    pa_fill_ellipse(c, sp, y + h * 0.70f, w * 0.15f, h * 0.025f, pa_hex(0xFF7A1A));
    pa_fill_ellipse(c, sp, y + h * 0.70f, w * 0.115f, h * 0.012f, pa_shade(pa_hex(0xFF7A1A), -0.3f));
    float pull = cycle < 0.3f ? 0.0f : pa_clamp01((cycle - 0.3f) * 6.0f) * w * 0.6f;
    float al = cycle < 0.3f ? 1.0f : 1.0f - pa_clamp01((cycle - 0.3f) * 4.0f);
    PA_Color blue = pa_alpha(pa_hex(0x1E7FD6), al);
    pa_line(c, tx0 + pull, mid, tx1 + w * 0.08f + pull, mid, w * 0.018f, blue);
    pa_stroke_circle(c, tx1 + w * 0.11f + pull, mid, w * 0.03f, w * 0.012f, blue);
}

const PA_Game PA_GAME_PINS = {
    "pins", "Pin Rescue", "Puzzle",
    "Pull the pins in the right order. Colour every ball and fill the cup.",
    PA_RGB(30, 127, 214),
    s_start, s_stop, s_update, s_render, s_thumb
};
