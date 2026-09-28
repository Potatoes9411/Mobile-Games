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
extern const PA_Game PA_GAME_ROADHOPPER;
extern const PA_Game PA_GAME_VOIDMUNCHER;
extern const PA_Game PA_GAME_CHROMERUSH;
extern const PA_Game PA_GAME_BLOCKSTORM;
extern const PA_Game PA_GAME_HELIX;

static const PA_Game *const GAMES[] = {
    &PA_GAME_ROADHOPPER,
    &PA_GAME_VOIDMUNCHER,
    &PA_GAME_CHROMERUSH,
    &PA_GAME_BLOCKSTORM,
    &PA_GAME_HELIX,
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
static float g_scroll;          /* pixels the grid is shifted up by */
static float g_scroll_max;
static float g_scroll_vel;
static int   g_scroll_active;   /* a drag is under way, so no tap fires */
static float g_scroll_from, g_drag_from_y, g_drag_travel;

static void layout_cards(void) {
    float pad = 18.0f;
    int cols = g_view_w >= 720 ? 3 : 2;
    float gap = 14.0f;
    float cw = ((float)g_view_w - pad * 2.0f - gap * (float)(cols - 1)) / (float)cols;
    float chh = cw * 1.12f;
    float top = 196.0f;
    int rows = (GAME_COUNT + cols - 1) / cols;

    /* How far the grid can travel before its last row sits on the bottom edge.
       Computed from the layout rather than guessed, so adding a game needs no
       further thought. */
    float content = top + (float)rows * (chh + gap) + 24.0f;
    g_scroll_max = content - (float)g_view_h;
    if (g_scroll_max < 0.0f) g_scroll_max = 0.0f;
    g_scroll = pa_clampf(g_scroll, 0.0f, g_scroll_max);

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
    pa_save_load();
    pa_audio_set_volume((float)pa_save_get("volume", 80) / 100.0f);
}

void pa_app_shutdown(void) {
    if (g_active && g_active->stop) g_active->stop();
    g_active = NULL;
    pa_save_flush();
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
static void draw_home(PA_Canvas *c) {
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

    /* Clip the grid below the header, or a scrolled card slides up over the
       wordmark. */
    int keep[4] = { c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1 };
    pa_clip_rect(c, 0, 186, c->w, c->h - 186);

    for (int i = 0; i < GAME_COUNT; i++) {
        Rect r = g_cards[i];
        const PA_Game *g = GAMES[i];
        int hot = (g_hover == i);
        if (r.y > (float)c->h + 40.0f || r.y + r.h < 150.0f) continue;

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

    c->clip_x0 = keep[0]; c->clip_y0 = keep[1];
    c->clip_x1 = keep[2]; c->clip_y1 = keep[3];

    /* Scrollbar, only when there is somewhere to go. */
    if (g_scroll_max > 0.0f) {
        float track_top = 200.0f;
        float track_h = (float)c->h - track_top - 26.0f;
        float thumb_h = track_h * pa_clamp01((float)c->h / ((float)c->h + g_scroll_max));
        if (thumb_h < 32.0f) thumb_h = 32.0f;
        float t = g_scroll / g_scroll_max;
        pa_round_rect(c, (float)c->w - 8.0f, track_top, 3.0f, track_h, 1.5f,
                      PA_RGBA(255, 255, 255, 18));
        pa_round_rect(c, (float)c->w - 9.0f, track_top + t * (track_h - thumb_h), 5.0f,
                      thumb_h, 2.5f, PA_RGBA(255, 255, 255, 70));
    }

}

/* ----------------------------------------------------------------- frame -- */
/* Set when the MENU button is tapped; acted on at render time so the teardown
   never happens midway through a batch of fixed update steps. */
static int g_pending_quit_home;

void pa_app_update(float dt, const PA_Input *in) {
    g_time += dt;
    if (g_fade > 0.0f) g_fade = pa_clampf(g_fade - dt * 3.4f, 0.0f, 1.0f);

    if (g_screen == SCREEN_HOME) {
        /* Drag to scroll the grid. The drag is tracked from where it started
           rather than integrated frame to frame, so a fast flick cannot drift
           away from the finger. */
        /*
         * Wheel first. Drag-to-scroll alone was a mobile assumption: on a
         * desktop the wheel is the first thing anyone reaches for, and with no
         * handler for it the grid simply appeared frozen.
         */
        if (in->wheel != 0.0f && g_scroll_max > 0.0f) {
            g_scroll = pa_clampf(g_scroll - in->wheel * 96.0f, 0.0f, g_scroll_max);
            g_scroll_vel = 0.0f;
        }

        /* Keyboard, for the same reason. */
        if (g_scroll_max > 0.0f) {
            float key_scroll = 0.0f;
            if (in->keys[PA_KEY_DOWN]) key_scroll += 1.0f;
            if (in->keys[PA_KEY_UP])   key_scroll -= 1.0f;
            if (key_scroll != 0.0f) {
                g_scroll = pa_clampf(g_scroll + key_scroll * 900.0f * dt, 0.0f, g_scroll_max);
                g_scroll_vel = 0.0f;
            }
            if (in->key_pressed[PA_KEY_PAGEDOWN]) g_scroll = pa_clampf(g_scroll + (float)g_view_h * 0.8f, 0.0f, g_scroll_max);
            if (in->key_pressed[PA_KEY_PAGEUP])   g_scroll = pa_clampf(g_scroll - (float)g_view_h * 0.8f, 0.0f, g_scroll_max);
            if (in->key_pressed[PA_KEY_HOME])     g_scroll = 0.0f;
            if (in->key_pressed[PA_KEY_END])      g_scroll = g_scroll_max;
        }

        if (in->pressed) {
            g_drag_from_y = in->y;
            g_scroll_from = g_scroll;
            g_drag_travel = 0.0f;
            g_scroll_active = 0;
            g_scroll_vel = 0.0f;
        }
        if (in->down && g_scroll_max > 0.0f) {
            float moved = g_drag_from_y - in->y;
            if (fabsf(moved) > g_drag_travel) g_drag_travel = fabsf(moved);
            /* Only once the finger has clearly travelled does this become a
               scroll; below that it is still a tap on a card. */
            if (g_drag_travel > 6.0f) {
                g_scroll_active = 1;
                float next = pa_clampf(g_scroll_from + moved, 0.0f, g_scroll_max);
                g_scroll_vel = (next - g_scroll) / (dt > 0.0001f ? dt : 0.0001f);
                g_scroll = next;
            }
        }
        if (!in->down) {
            /* Momentum, then a stop. Without it the grid feels nailed down. */
            g_scroll = pa_clampf(g_scroll + g_scroll_vel * dt, 0.0f, g_scroll_max);
            g_scroll_vel *= expf(-6.0f * dt);
            if (fabsf(g_scroll_vel) < 4.0f) g_scroll_vel = 0.0f;
        }

        g_hover = -1;
        layout_cards();
        for (int i = 0; i < GAME_COUNT; i++) {
            Rect r = g_cards[i];
            if (in->x >= r.x && in->x <= r.x + r.w && in->y >= r.y && in->y <= r.y + r.h) {
                g_hover = i;
                /* A tap, not a bare release: a drag that happens to end over a
                   card should not launch it, and a release carrying a stale
                   pointer position should not launch anything at all. */
                if (in->tapped && !g_scroll_active) launch(i);
            }
        }
        if (in->released) g_scroll_active = 0;
        if (in->key_pressed[PA_KEY_ESC]) g_quit = 1;
        return;
    }

    if (in->key_pressed[PA_KEY_ESC]) {
        g_screen = (g_screen == SCREEN_PAUSE) ? SCREEN_GAME : SCREEN_PAUSE;
        pa_sfx("select");
    }
    if (in->tapped && in->x < 96.0f && in->y < 56.0f) { g_pending_quit_home = 1; return; }

    /* A paused game keeps painting but stops updating, so the overlay sits over
       a live scene rather than a frozen buffer. */
    if (g_active && g_screen == SCREEN_GAME && g_active->update) g_active->update(dt, in);
}

void pa_app_render(PA_Canvas *c) {
    if (g_view_w != c->w || g_view_h != c->h) {
        g_view_w = c->w;
        g_view_h = c->h;
        layout_cards();
    }
    if (g_pending_quit_home) { g_pending_quit_home = 0; go_home(); }

    if (g_screen == SCREEN_HOME) {
        draw_home(c);
    } else {
        if (g_active && g_active->render) g_active->render(c);

        /* Menu button, top left, matching the browser build. */
        pa_round_rect(c, 14.0f, 14.0f, 74.0f, 32.0f, 10.0f, PA_RGBA(14, 11, 30, 200));
        pa_text(c, "MENU", 51.0f, 36.0f, 13.0f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.0f);

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
