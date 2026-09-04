/* ===========================================================================
   POCKET ARCADE - native hub
   The shell around the games: the grid of animated cards, the launch and exit
   transitions, and the pause overlay. Games are registered in one table; adding
   one is a line here and a file in games/.
   =========================================================================== */
#include "pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern const PA_Game PA_GAME_SPLAT;

static const PA_Game *const GAMES[] = {
    &PA_GAME_SPLAT
};
#define GAME_COUNT ((int)(sizeof(GAMES) / sizeof(GAMES[0])))

typedef enum { SCREEN_HOME, SCREEN_GAME, SCREEN_PAUSE } Screen;

static Screen         g_screen;
static const PA_Game *g_active;
static float          g_time;
static float          g_fade;        /* 1 while a transition is masking a swap */
static int            g_quit;
static int            g_view_w, g_view_h;
static int            g_hover = -1;

/* Card geometry, resolved once per layout change so hit testing and drawing
   can never disagree about where a card is. */
typedef struct { float x, y, w, h; } Rect;
static Rect  g_cards[GAME_COUNT];
static float g_scroll;

static void layout_cards(void) {
    float pad = 18.0f;
    int cols = g_view_w >= 720 ? 3 : 2;
    float gap = 14.0f;
    float cw = ((float)g_view_w - pad * 2.0f - gap * (float)(cols - 1)) / (float)cols;
    float chh = cw * 1.12f;
    float top = 196.0f;

    for (int i = 0; i < GAME_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        g_cards[i].x = pad + (float)col * (cw + gap);
        g_cards[i].y = top + (float)row * (chh + gap) - g_scroll;
        g_cards[i].w = cw;
        g_cards[i].h = chh;
    }
}

/* Headless capture entry: launches a game straight from the command line so the
   build can be verified from CI, where nothing can click a card. */
void pa_app_debug_launch(int index) {
    if (index >= 0 && index < GAME_COUNT) {
        g_active = GAMES[index];
        if (g_active->start) g_active->start();
        g_screen = SCREEN_GAME;
        g_fade = 0.0f;
    }
}

void pa_app_init(int w, int h) {
    g_view_w = w;
    g_view_h = h;
    g_screen = SCREEN_HOME;
    g_active = NULL;
    g_time = 0.0f;
    g_fade = 0.0f;
    g_quit = 0;
    g_scroll = 0.0f;
    layout_cards();
    pa_audio_set_volume(0.8f);
}

void pa_app_shutdown(void) {
    if (g_active && g_active->stop) g_active->stop();
    g_active = NULL;
}

int pa_app_should_quit(void) { return g_quit; }

static void launch(int index) {
    if (index < 0 || index >= GAME_COUNT) return;
    g_active = GAMES[index];
    if (g_active->start) g_active->start();
    g_screen = SCREEN_GAME;
    g_fade = 1.0f;
    pa_sfx("select");
}

static void go_home(void) {
    if (g_active && g_active->stop) g_active->stop();
    g_active = NULL;
    g_screen = SCREEN_HOME;
    g_fade = 1.0f;
    pa_sfx("select");
}

/* ------------------------------------------------------------------ home -- */
static void draw_home(PA_Canvas *c, const PA_Input *in) {
    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(0x1A1436));
    pa_stop(&bg, 0.55f, pa_hex(0x120E28));
    pa_stop(&bg, 1.0f, pa_hex(0x0B0818));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);

    /* A slow drifting glow, so an idle menu is not a still image. */
    for (int i = 0; i < 2; i++) {
        float t = g_time * 0.12f + (float)i * 2.1f;
        float gx = (float)c->w * (0.30f + 0.40f * (0.5f + 0.5f * sinf(t)));
        float gy = (float)c->h * (0.18f + 0.10f * (0.5f + 0.5f * cosf(t * 1.3f)));
        float gr = (float)c->w * 0.55f;
        PA_Paint glow = pa_radial(gx, gy, 0.0f, gr);
        pa_stop(&glow, 0.0f, PA_RGBA(93, 224, 255, i ? 22 : 30));
        pa_stop(&glow, 1.0f, PA_RGBA(93, 224, 255, 0));
        pa_fill_ellipse_paint(c, gx, gy, gr, gr, &glow);
    }

    pa_text(c, "POCKET", 24.0f, 56.0f, 34.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 6.0f);
    float wm = pa_text_width("POCKET", 34.0f, 6.0f);
    pa_text(c, "ARCADE", 24.0f + wm + 14.0f, 56.0f, 34.0f,
            PA_RGB(93, 224, 255), PA_ALIGN_LEFT, 6.0f);

    pa_text(c, "NATIVE BUILD  -  NO BROWSER", 24.0f, 108.0f, 12.0f,
            PA_RGBA(255, 255, 255, 110), PA_ALIGN_LEFT, 4.0f);

    pa_fill_rect(c, 24.0f, 140.0f, (float)c->w - 48.0f, 2.0f, PA_RGBA(255, 255, 255, 26));
    pa_text(c, "ALL GAMES", 24.0f, 168.0f, 15.0f,
            PA_RGBA(255, 255, 255, 190), PA_ALIGN_LEFT, 5.0f);

    layout_cards();
    for (int i = 0; i < GAME_COUNT; i++) {
        Rect r = g_cards[i];
        const PA_Game *g = GAMES[i];
        int hot = (g_hover == i);

        pa_round_rect(c, r.x, r.y + 3.0f, r.w, r.h, 14.0f, PA_RGBA(0, 0, 0, 90));
        pa_round_rect(c, r.x, r.y, r.w, r.h, 14.0f, pa_hex(0x1C1740));

        /* Thumbnail occupies the top of the card, clipped to its rounded top. */
        float th = r.h * 0.60f;
        int saved[4] = { c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1 };
        pa_clip_rect(c, (int)r.x + 1, (int)r.y + 1, (int)r.w - 2, (int)th);
        if (g->thumb) g->thumb(c, r.x, r.y, r.w, th, g_time + (float)i * 1.7f);
        c->clip_x0 = saved[0]; c->clip_y0 = saved[1];
        c->clip_x1 = saved[2]; c->clip_y1 = saved[3];

        pa_fill_rect(c, r.x, r.y + th, r.w, 2.0f, PA_RGBA(255, 255, 255, 30));

        pa_text(c, g->name, r.x + 12.0f, r.y + th + 24.0f, 15.0f,
                PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.0f);
        pa_text(c, g->genre, r.x + 12.0f, r.y + th + 48.0f, 10.0f,
                g->accent, PA_ALIGN_LEFT, 3.0f);

        pa_round_rect(c, r.x + 10.0f, r.y + 10.0f, 54.0f, 22.0f, 7.0f,
                      PA_RGBA(10, 8, 24, 190));
        pa_text(c, "PLAY", r.x + 37.0f, r.y + 25.0f, 11.0f,
                g->accent, PA_ALIGN_CENTER, 2.0f);

        pa_stroke_poly(c, (PA_Vec2[]){
            { r.x, r.y }, { r.x + r.w, r.y }, { r.x + r.w, r.y + r.h }, { r.x, r.y + r.h }
        }, 4, 1, hot ? 2.6f : 1.4f, hot ? g->accent : PA_RGBA(255, 255, 255, 34));
    }

    (void)in;
}

/* ----------------------------------------------------------------- frame -- */
void pa_app_frame(PA_Canvas *c, float dt, const PA_Input *in) {
    g_time += dt;
    if (g_view_w != c->w || g_view_h != c->h) {
        g_view_w = c->w;
        g_view_h = c->h;
        layout_cards();
    }
    if (g_fade > 0.0f) g_fade = pa_clampf(g_fade - dt * 3.4f, 0.0f, 1.0f);

    if (g_screen == SCREEN_HOME) {
        g_hover = -1;
        for (int i = 0; i < GAME_COUNT; i++) {
            Rect r = g_cards[i];
            if (in->x >= r.x && in->x <= r.x + r.w && in->y >= r.y && in->y <= r.y + r.h) {
                g_hover = i;
                if (in->released) launch(i);
            }
        }
        if (in->key_pressed[PA_KEY_ESC]) g_quit = 1;
        draw_home(c, in);
    } else {
        if (in->key_pressed[PA_KEY_ESC]) {
            g_screen = (g_screen == SCREEN_PAUSE) ? SCREEN_GAME : SCREEN_PAUSE;
            pa_sfx("select");
        }

        if (g_active) {
            /* A paused game keeps painting but stops updating, so the overlay
               sits over a live scene rather than a frozen buffer. */
            if (g_screen == SCREEN_GAME && g_active->update) g_active->update(dt, in);
            if (g_active->render) g_active->render(c);
        }

        /* Menu button, top left, matching the browser build. */
        pa_round_rect(c, 14.0f, 14.0f, 74.0f, 32.0f, 10.0f, PA_RGBA(14, 11, 30, 200));
        pa_text(c, "MENU", 51.0f, 36.0f, 13.0f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.0f);
        if (in->released && in->x < 96.0f && in->y < 56.0f) { go_home(); return; }

        if (g_screen == SCREEN_PAUSE) {
            pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 8, 24, 220));
            pa_text(c, "PAUSED", (float)c->w * 0.5f, (float)c->h * 0.42f, 40.0f,
                    PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 8.0f);
            pa_text(c, "ESC TO RESUME", (float)c->w * 0.5f, (float)c->h * 0.52f, 14.0f,
                    PA_RGBA(255, 255, 255, 150), PA_ALIGN_CENTER, 5.0f);
        }
    }

    if (g_fade > 0.0f) {
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h,
                     PA_RGBA(8, 6, 18, (int)(g_fade * 255.0f)));
    }
}
