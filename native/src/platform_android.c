/* ===========================================================================
   POCKET ARCADE — Android platform layer (NativeActivity + native_app_glue)
   ANativeWindow software blit, touch input, AAudio (via audio.c), lifecycle.
   =========================================================================== */
#ifdef __ANDROID__

#include "pa.h"
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <jni.h>

#define TAG "PocketArcade"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* -------------------------------------------------------- timing --------- */
#define FIXED_STEP (1.0 / 120.0)
#define MAX_STEPS  8

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* -------------------------------------------------------- state ---------- */
static PA_Canvas  g_canvas;
static PA_Input   g_input;
static int        g_running;       /* window is usable */
static int        g_focused;       /* has input focus */
static double     g_accumulator;
static double     g_previous;
static struct android_app *g_app;

/* touch tracking for swipe/tap classification */
static float g_press_x, g_press_y;
static double g_press_time;
static int   g_pending_press, g_pending_release;
static float g_prev_x, g_prev_y;

/* --------------------------------------------------- frame helpers ------- */
static void begin_frame(void) {
    g_input.dx = g_input.x - g_prev_x;
    g_input.dy = g_input.y - g_prev_y;
    g_prev_x = g_input.x;
    g_prev_y = g_input.y;

    g_input.pressed  = g_pending_press;
    g_input.released = g_pending_release;
    g_input.tapped   = 0;
    g_input.swipe    = PA_SWIPE_NONE;
    g_input.wheel    = 0.0f;

    if (g_pending_release) {
        float dx = g_input.x - g_press_x;
        float dy = g_input.y - g_press_y;
        float d2 = dx * dx + dy * dy;
        double held = now_seconds() - g_press_time;
        if (d2 > 30.0f * 30.0f && held < 0.6) {
            if (dx * dx > dy * dy)
                g_input.swipe = dx > 0 ? PA_SWIPE_RIGHT : PA_SWIPE_LEFT;
            else
                g_input.swipe = dy > 0 ? PA_SWIPE_DOWN : PA_SWIPE_UP;
        } else {
            g_input.swipe = PA_SWIPE_TAP;
            g_input.tapped = 1;
        }
    }
    g_pending_press = 0;
    g_pending_release = 0;
}

/* -------------------------------------------------- input handling ------- */
static int32_t on_input(struct android_app *app, AInputEvent *event) {
    (void)app;
    int32_t type = AInputEvent_getType(event);
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    /* Scale touch coordinates to canvas space */
    if (g_canvas.w > 0 && g_canvas.h > 0 && g_app && g_app->window) {
        int ww = ANativeWindow_getWidth(g_app->window);
        int wh = ANativeWindow_getHeight(g_app->window);
        if (ww > 0 && wh > 0) {
            x = x * (float)g_canvas.w / (float)ww;
            y = y * (float)g_canvas.h / (float)wh;
        }
    }

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            g_input.x = x;
            g_input.y = y;
            g_press_x = x;
            g_press_y = y;
            g_press_time = now_seconds();
            g_input.down = 1;
            g_pending_press = 1;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            g_input.x = x;
            g_input.y = y;
            g_input.down = 0;
            g_pending_release = 1;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            g_input.x = x;
            g_input.y = y;
            break;
    }
    return 1;
}

/* ------------------------------------------------ ANativeWindow blit ----- */
static void present(void) {
    if (!g_app->window) return;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(g_app->window, &buf, NULL) != 0) return;

    int dw = buf.width;
    int dh = buf.height;
    int sw = g_canvas.w;
    int sh = g_canvas.h;

    uint32_t *dst = (uint32_t *)buf.bits;
    uint32_t *src = g_canvas.px;

    if (dw == sw && dh == sh && buf.stride == dw) {
        /* Fast path: dimensions match exactly */
        memcpy(dst, src, (size_t)(sw * sh) * 4);
    } else {
        /* Scale blit: nearest-neighbour stretch */
        for (int y = 0; y < dh; y++) {
            int sy = y * sh / dh;
            if (sy >= sh) sy = sh - 1;
            uint32_t *srow = src + sy * sw;
            uint32_t *drow = dst + y * buf.stride;
            for (int x = 0; x < dw; x++) {
                int sx = x * sw / dw;
                if (sx >= sw) sx = sw - 1;
                /* Convert 0x00RRGGBB → 0xFFRRGGBB (ABGR → set alpha, keep
                   RGB since ANativeWindow format RGBA_8888 is actually ABGR
                   in memory on little-endian). Actually Android's WINDOW_FORMAT
                   _RGBA_8888 is R in low byte, so we need to swap R and B. */
                uint32_t c = srow[sx];
                uint32_t r = (c >> 16) & 0xFF;
                uint32_t g = (c >> 8) & 0xFF;
                uint32_t b = c & 0xFF;
                drow[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
            }
        }
    }

    ANativeWindow_unlockAndPost(g_app->window);
}

/* -------------------------------------------- lifecycle commands --------- */
static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                int w = ANativeWindow_getWidth(app->window);
                int h = ANativeWindow_getHeight(app->window);
                ANativeWindow_setBuffersGeometry(app->window, w, h,
                    WINDOW_FORMAT_RGBA_8888);

                if (g_canvas.px) {
                    pa_canvas_resize(&g_canvas, w, h);
                } else {
                    pa_canvas_init(&g_canvas, w, h);
                    pa_app_init(w, h);
                }
                g_running = 1;
                g_previous = now_seconds();
                LOGI("Window init %dx%d", w, h);
            }
            break;

        case APP_CMD_TERM_WINDOW:
            g_running = 0;
            break;

        case APP_CMD_GAINED_FOCUS:
            g_focused = 1;
            pa_audio_pause(0);
            break;

        case APP_CMD_LOST_FOCUS:
            g_focused = 0;
            pa_audio_pause(1);
            break;

        case APP_CMD_PAUSE:
            pa_audio_pause(1);
            break;

        case APP_CMD_RESUME:
            if (g_focused) pa_audio_pause(0);
            g_previous = now_seconds();
            g_accumulator = 0.0;
            break;

        case APP_CMD_DESTROY:
            pa_app_shutdown();
            pa_audio_shutdown();
            pa_canvas_free(&g_canvas);
            break;

        case APP_CMD_WINDOW_RESIZED:
            if (app->window) {
                int w = ANativeWindow_getWidth(app->window);
                int h = ANativeWindow_getHeight(app->window);
                ANativeWindow_setBuffersGeometry(app->window, w, h,
                    WINDOW_FORMAT_RGBA_8888);
                pa_canvas_resize(&g_canvas, w, h);
                LOGI("Window resize %dx%d", w, h);
            }
            break;
    }
}

/* ------------------------------------------ internal data dir helper ----- */
static void set_data_dir(struct android_app *app) {
    /* NativeActivity exposes internalDataPath via the ANativeActivity struct */
    if (app->activity && app->activity->internalDataPath) {
        pa_save_set_dir(app->activity->internalDataPath);
        LOGI("Save dir: %s", app->activity->internalDataPath);
    }
}

/* ======================================= android_main (app entry) ======== */
void android_main(struct android_app *app) {
    g_app = app;
    memset(&g_canvas, 0, sizeof(g_canvas));
    memset(&g_input, 0, sizeof(g_input));
    g_running = 0;
    g_focused = 0;
    g_accumulator = 0.0;

    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    set_data_dir(app);
    pa_save_load();
    pa_audio_init();

    LOGI("Pocket Arcade starting");

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *source;

        /* If we have no window or no focus, block until we get a lifecycle
           event.  Otherwise poll without blocking so we can keep rendering. */
        int timeout = (g_running && g_focused) ? 0 : -1;

        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
            /* After the first event, switch to non-blocking so we don't
               starve the render loop while events are queued. */
            timeout = 0;
        }
        if (app->destroyRequested) break;

        if (!g_running || !g_focused) continue;

        double current = now_seconds();
        double frame = current - g_previous;
        g_previous = current;
        if (frame > 0.25) frame = 0.25;
        if (frame < 0.0) frame = 0.0;

        begin_frame();

        g_accumulator += frame;
        int steps = 0;
        while (g_accumulator >= FIXED_STEP && steps < MAX_STEPS) {
            pa_app_update((float)FIXED_STEP, &g_input);
            g_accumulator -= FIXED_STEP;
            steps++;
            g_input.pressed  = 0;
            g_input.released = 0;
            g_input.tapped   = 0;
            g_input.swipe    = PA_SWIPE_NONE;
            g_input.wheel    = 0.0f;
            for (int k = 0; k < PA_KEY_COUNT; k++) g_input.key_pressed[k] = 0;
        }
        if (steps >= MAX_STEPS) g_accumulator = 0.0;

        pa_app_render(&g_canvas);
        present();

        /* Yield a little so we don't burn the CPU at 100% */
        double spent = now_seconds() - current;
        if (spent < FIXED_STEP) {
            usleep((useconds_t)((FIXED_STEP - spent) * 1000000.0));
        }
    }

    LOGI("Pocket Arcade exiting");
}

#endif /* __ANDROID__ */
