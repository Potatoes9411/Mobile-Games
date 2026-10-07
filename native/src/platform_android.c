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
    if (type == AINPUT_EVENT_TYPE_KEY) {
        /* The system back button pauses a game, and backs out of the pause
           sheet; on the arcade grid it is left to the system. */
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
            if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
                g_input.keys[PA_KEY_ESC] = 0;
                return pa_app_in_game();
            }
            if (!g_input.keys[PA_KEY_ESC]) g_input.key_pressed[PA_KEY_ESC] = 1;
            g_input.keys[PA_KEY_ESC] = 1;
            return pa_app_in_game();
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    /* Scale touch coordinates to canvas space */
    if (g_canvas.w > 0 && g_phys_w > 0 && g_phys_h > 0) {
        x = x * (float)g_canvas.w / (float)g_phys_w;
        y = y * (float)g_canvas.h / (float)g_phys_h;
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
    int w = buf.width < g_canvas.w ? buf.width : g_canvas.w;
    int h = buf.height < g_canvas.h ? buf.height : g_canvas.h;
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = g_canvas.px + (size_t)y * g_canvas.w;
        uint32_t *drow = (uint32_t *)buf.bits + (size_t)y * buf.stride;
        /* Canvas is 0x00RRGGBB; RGBA_8888 is R,G,B,A in memory. */
        for (int x = 0; x < w; x++) {
            uint32_t c = srow[x];
            drow[x] = 0xFF000000u | ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
        }
    }
    ANativeWindow_unlockAndPost(g_app->window);
}

/* The canvas runs at a logical size whose short side is LOGICAL_SHORT pixels,
   the size every game is laid out and reviewed at. The window's buffer is set
   to that size and the system compositor scales it to the panel on the GPU,
   so a 1440p phone costs the software rasterizer the same as a 720p one and
   UI measured in pixels is the same physical size everywhere. */
#define LOGICAL_SHORT 540
static int g_phys_w, g_phys_h;

static void configure_window(ANativeWindow *win) {
    ANativeWindow_setBuffersGeometry(win, 0, 0, WINDOW_FORMAT_RGBA_8888);
    g_phys_w = ANativeWindow_getWidth(win);
    g_phys_h = ANativeWindow_getHeight(win);
    if (g_phys_w <= 0 || g_phys_h <= 0) return;
    int lw, lh;
    if (g_phys_w <= g_phys_h) { lw = LOGICAL_SHORT; lh = (int)((float)g_phys_h * LOGICAL_SHORT / (float)g_phys_w + 0.5f); }
    else                      { lh = LOGICAL_SHORT; lw = (int)((float)g_phys_w * LOGICAL_SHORT / (float)g_phys_h + 0.5f); }
    ANativeWindow_setBuffersGeometry(win, lw, lh, WINDOW_FORMAT_RGBA_8888);
    if (g_canvas.px) pa_canvas_resize(&g_canvas, lw, lh);
    else { pa_canvas_init(&g_canvas, lw, lh); pa_app_init(lw, lh); }
    LOGI("Window %dx%d -> canvas %dx%d", g_phys_w, g_phys_h, lw, lh);
}

/* Ask the activity for a landscape or portrait lock through JNI; NativeActivity
   has no native call for it. */
int pa_demo_mode(void) { return 0; }

void pa_set_landscape(int on) {
    if (!g_app || !g_app->activity) return;
    JavaVM *vm = g_app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || !env) return;
    jobject act = g_app->activity->clazz;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, "setRequestedOrientation", "(I)V");
    /* SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6, SENSOR_PORTRAIT = 7 */
    if (m) (*env)->CallVoidMethod(env, act, m, on ? 6 : 7);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*vm)->DetachCurrentThread(vm);
}

/* -------------------------------------------- lifecycle commands --------- */
static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                configure_window(app->window);
                g_running = 1;
                g_previous = now_seconds();
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
        case APP_CMD_CONFIG_CHANGED:
            if (app->window) configure_window(app->window);
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
