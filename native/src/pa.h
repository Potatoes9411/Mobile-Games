/* ===========================================================================
   POCKET ARCADE - native engine, public header
   A self-contained 2D engine for Windows. No browser, no runtime, no engine
   dependency: a Win32 window, a software rasterizer writing into a 32-bit
   framebuffer, a WinMM tone synth, and the games on top.

   The drawing API deliberately mirrors the shape of the Canvas 2D calls the
   browser build uses, so a game's rendering code ports across as a mechanical
   translation rather than a redesign.
   =========================================================================== */
#ifndef POCKET_ARCADE_H
#define POCKET_ARCADE_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ math -- */
#define PA_TAU 6.28318530717958647692f
#define PA_PI  3.14159265358979323846f

typedef struct { float x, y; } PA_Vec2;

float pa_clampf(float v, float lo, float hi);
float pa_clamp01(float v);
float pa_lerpf(float a, float b, float t);
float pa_smooth(float t);
/** Frame-rate independent approach, the same curve the browser build uses. */
float pa_approach(float current, float target, float rate, float dt);
float pa_wrapf(float v, float m);

/* Deterministic RNG, mulberry32 - identical stream to the JS build. */
typedef struct { uint32_t state; } PA_Rng;
void     pa_rng_seed(PA_Rng *r, uint32_t seed);
float    pa_rng_next(PA_Rng *r);          /* [0,1) */
float    pa_rng_range(PA_Rng *r, float lo, float hi);
int      pa_rng_int(PA_Rng *r, int lo, int hi);   /* inclusive */
int      pa_rng_chance(PA_Rng *r, float p);

/* ---------------------------------------------------------------- colour -- */
/* Colours are 0xAARRGGBB, premultiplied nowhere - the blender does the work. */
typedef uint32_t PA_Color;

#define PA_RGB(r, g, b)      (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PA_RGBA(r, g, b, a)  (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PA_A(c) (((c) >> 24) & 0xFFu)
#define PA_R(c) (((c) >> 16) & 0xFFu)
#define PA_G(c) (((c) >> 8) & 0xFFu)
#define PA_B(c) ((c) & 0xFFu)

PA_Color pa_hex(uint32_t rgb);
PA_Color pa_mix(PA_Color a, PA_Color b, float t);
/** Positive lightens toward white, negative darkens toward black. */
PA_Color pa_shade(PA_Color c, float amount);
PA_Color pa_alpha(PA_Color c, float a);
PA_Color pa_hsl(float h, float s, float l);

/* ---------------------------------------------------------------- canvas -- */
typedef struct {
    int       w, h;
    uint32_t *px;         /* w * h, 0x00RRGGBB (the alpha byte is unused) */
    /* Scissor rectangle. Every write is clipped to this, which is how the hub
       keeps a game from painting over the overlay. */
    int       clip_x0, clip_y0, clip_x1, clip_y1;
} PA_Canvas;

int  pa_canvas_init(PA_Canvas *c, int w, int h);
void pa_canvas_free(PA_Canvas *c);
int  pa_canvas_resize(PA_Canvas *c, int w, int h);
void pa_clip_reset(PA_Canvas *c);
void pa_clip_rect(PA_Canvas *c, int x, int y, int w, int h);

void pa_clear(PA_Canvas *c, PA_Color colour);

/* --------------------------------------------------------------- shading -- */
/*
 * A paint is either a flat colour or a gradient. Passing the paint into the
 * scanline filler rather than resolving it up front is what lets a gradient
 * cost the same as a flat fill: the span loop evaluates it per pixel with two
 * multiplies, and no temporary surface is ever allocated.
 */
typedef enum { PA_PAINT_FLAT, PA_PAINT_LINEAR, PA_PAINT_RADIAL } PA_PaintKind;

#define PA_MAX_STOPS 6

typedef struct {
    PA_PaintKind kind;
    PA_Color     flat;
    float        x0, y0, x1, y1;    /* linear: endpoints. radial: centre + r1 */
    float        r0, r1;
    int          stop_count;
    float        stop_pos[PA_MAX_STOPS];
    PA_Color     stop_col[PA_MAX_STOPS];
} PA_Paint;

PA_Paint pa_flat(PA_Color c);
PA_Paint pa_linear(float x0, float y0, float x1, float y1);
PA_Paint pa_radial(float cx, float cy, float r0, float r1);
void     pa_stop(PA_Paint *p, float pos, PA_Color c);

/* ------------------------------------------------------------- primitives -- */
void pa_fill_rect(PA_Canvas *c, float x, float y, float w, float h, PA_Color col);
void pa_fill_rect_paint(PA_Canvas *c, float x, float y, float w, float h, const PA_Paint *p);

/* Polygons are filled with the non-zero winding rule and analytic coverage
   antialiasing, which is what keeps the extruded-box art from crawling. */
void pa_fill_poly(PA_Canvas *c, const PA_Vec2 *pts, int n, PA_Color col);
void pa_fill_poly_paint(PA_Canvas *c, const PA_Vec2 *pts, int n, const PA_Paint *p);

void pa_fill_circle(PA_Canvas *c, float cx, float cy, float r, PA_Color col);
void pa_fill_ellipse(PA_Canvas *c, float cx, float cy, float rx, float ry, PA_Color col);
void pa_fill_ellipse_paint(PA_Canvas *c, float cx, float cy, float rx, float ry, const PA_Paint *p);
void pa_round_rect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col);
void pa_round_rect_paint(PA_Canvas *c, float x, float y, float w, float h, float r, const PA_Paint *p);

void pa_stroke_poly(PA_Canvas *c, const PA_Vec2 *pts, int n, int closed, float width, PA_Color col);
void pa_stroke_rect(PA_Canvas *c, float x, float y, float w, float h, float width, PA_Color col);
void pa_stroke_circle(PA_Canvas *c, float cx, float cy, float r, float width, PA_Color col);
void pa_line(PA_Canvas *c, float x0, float y0, float x1, float y1, float width, PA_Color col);

/* ------------------------------------------------------------------ text -- */
/*
 * The font is stroke defined rather than a bitmap, so it stays crisp at any
 * size and ships as a few hundred bytes of coordinates instead of an atlas.
 */
typedef enum { PA_ALIGN_LEFT, PA_ALIGN_CENTER, PA_ALIGN_RIGHT } PA_Align;

float pa_text_width(const char *text, float size, float tracking);
void  pa_text(PA_Canvas *c, const char *text, float x, float y, float size,
              PA_Color col, PA_Align align, float tracking);

/* ----------------------------------------------------------------- input -- */
typedef enum {
    PA_KEY_LEFT, PA_KEY_RIGHT, PA_KEY_UP, PA_KEY_DOWN,
    PA_KEY_SPACE, PA_KEY_ESC, PA_KEY_ENTER, PA_KEY_COUNT
} PA_Key;

typedef struct {
    float x, y;            /* pointer position in canvas pixels */
    float dx, dy;          /* movement since the previous frame */
    int   down;            /* held */
    int   pressed;         /* went down this frame */
    int   released;        /* came up this frame */
    int   tapped;          /* released without travelling far */
    int   swipe;           /* one of PA_SWIPE_* for the frame it lands */
    int   keys[PA_KEY_COUNT];
    int   key_pressed[PA_KEY_COUNT];
} PA_Input;

enum { PA_SWIPE_NONE = 0, PA_SWIPE_LEFT, PA_SWIPE_RIGHT, PA_SWIPE_UP, PA_SWIPE_DOWN, PA_SWIPE_TAP };

/* ----------------------------------------------------------------- audio -- */
void pa_audio_init(void);
void pa_audio_shutdown(void);
void pa_audio_set_volume(float v);
/** A swept tone. `shape` 0 sine, 1 triangle, 2 square, 3 saw. */
void pa_tone(float from_hz, float to_hz, float seconds, int shape, float gain);
void pa_noise(float seconds, float gain);
void pa_sfx(const char *name);

/* ------------------------------------------------------------------ save -- */
/*
 * A tiny key/value store persisted beside the executable. Scores and settings
 * are the only state worth keeping, and a plain text file means a corrupted or
 * hand-edited save degrades to defaults rather than to a crash.
 */
void  pa_save_load(void);
void  pa_save_flush(void);
int   pa_save_get(const char *key, int fallback);
void  pa_save_set(const char *key, int value);

/* ------------------------------------------------------------------ game -- */
typedef struct PA_Game PA_Game;

struct PA_Game {
    const char *id;
    const char *name;
    const char *genre;
    const char *tagline;
    PA_Color    accent;

    void  (*start)(void);
    void  (*stop)(void);
    void  (*update)(float dt, const PA_Input *in);
    void  (*render)(PA_Canvas *c);
    /* Animated tile art for the hub grid. */
    void  (*thumb)(PA_Canvas *c, float x, float y, float w, float h, float t);
};

/* Implemented by hub.c, called by the platform layer.
   Update and render are separate so the platform can run the simulation on a
   fixed timestep and draw once, which is what makes behaviour identical on a
   60Hz panel and a 240Hz one. */
void pa_app_init(int w, int h);
void pa_app_update(float dt, const PA_Input *in);
void pa_app_render(PA_Canvas *c);
void pa_app_shutdown(void);
/** Non-zero once the app wants the window closed. */
int  pa_app_should_quit(void);

#endif /* POCKET_ARCADE_H */
