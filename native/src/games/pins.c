/* Pin Rescue - placeholder until the native port lands. */
#include "../pa.h"

static void s_start(void) {}
static void s_stop(void) {}
static void s_update(float dt, const PA_Input *in) { (void)dt; (void)in; }
static void s_render(PA_Canvas *c) {
    pa_clear(c, PA_RGB(30, 30, 40));
    pa_text(c, "PIN RESCUE", (float)c->w * 0.5f, (float)c->h * 0.45f, 28.0f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 3.0f);
    pa_text(c, "COMING SOON", (float)c->w * 0.5f, (float)c->h * 0.52f, 14.0f, PA_RGBA(255, 255, 255, 150), PA_ALIGN_CENTER, 4.0f);
}
static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    (void)t;
    pa_fill_rect(c, x, y, w, h, PA_RGB(255, 190, 40));
}

const PA_Game PA_GAME_PINS = {
    "pins", "Pin Rescue", "Puzzle",
    "Pull the pins in the right order. Gold to the hero, lava away from him.",
    PA_RGB(255, 190, 40),
    s_start, s_stop, s_update, s_render, s_thumb
};
