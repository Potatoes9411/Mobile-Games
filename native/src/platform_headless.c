/* ===========================================================================
   POCKET ARCADE - headless capture platform (Linux/macOS, plain C99 + libm)
   Runs the real hub and games with no window: scripted touches in, PPM frames
   out. This is what every reviewer looks at, so it drives the exact same
   pa_app_update/pa_app_render path and fixed timestep as the shipping builds.

   pa_headless --game <id|index|hub> [--size W H] [--seconds S] [--hz HZ]
               [--auto MODE] [--demo N] [--input SCRIPT] [--shots T1,T2,...]
               --out PREFIX

   SCRIPT is ';'-separated events, each "time:verb[:args]":
     0.5:tap:X:Y        press+release at X,Y (canvas px)
     1.0:swipe:left|right|up|down[:X:Y]
     1.2:down:X:Y  1.3:move:X:Y  1.6:up      raw pointer
     2.0:key:left|right|up|down|space|enter|esc[:SECONDS]   hold a key
   Coordinates may be given as fractions (0..1) of the canvas: 0.5:tap:0.5f:0.8f
   =========================================================================== */
#include "pa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define FIXED_STEP (1.0 / 120.0)

void pa_app_debug_launch(int index);
int  pa_app_debug_find(const char *id);

typedef struct { double t; char verb[8]; char a[16]; float x, y; float dur; int has_xy; } Ev;
static Ev g_ev[512];
static int g_nev;
static PA_Canvas g_canvas;

static float coord(const char *s, float extent) {
    size_t n = strlen(s);
    if (n && s[n - 1] == 'f') return (float)atof(s) * extent;
    return (float)atof(s);
}

static void parse_script(const char *src) {
    char *buf = strdup(src), *save = NULL;
    for (char *tok = strtok_r(buf, ";", &save); tok && g_nev < 512; tok = strtok_r(NULL, ";", &save)) {
        Ev *e = &g_ev[g_nev];
        memset(e, 0, sizeof(*e));
        char *parts[6] = { 0 };
        int np = 0;
        char *s2 = NULL;
        for (char *p = strtok_r(tok, ":", &s2); p && np < 6; p = strtok_r(NULL, ":", &s2)) parts[np++] = p;
        if (np < 2) continue;
        e->t = atof(parts[0]);
        snprintf(e->verb, sizeof(e->verb), "%s", parts[1]);
        if (!strcmp(e->verb, "swipe") || !strcmp(e->verb, "key")) {
            if (np > 2) snprintf(e->a, sizeof(e->a), "%s", parts[2]);
            if (!strcmp(e->verb, "key")) e->dur = np > 3 ? (float)atof(parts[3]) : 0.1f;
            else if (np > 4) { e->x = coord(parts[3], (float)g_canvas.w); e->y = coord(parts[4], (float)g_canvas.h); e->has_xy = 1; }
        } else if (np > 3) {
            e->x = coord(parts[2], (float)g_canvas.w);
            e->y = coord(parts[3], (float)g_canvas.h);
            e->has_xy = 1;
        }
        g_nev++;
    }
    free(buf);
}

static int key_index(const char *k) {
    if (!strcmp(k, "left")) return PA_KEY_LEFT;
    if (!strcmp(k, "right")) return PA_KEY_RIGHT;
    if (!strcmp(k, "up")) return PA_KEY_UP;
    if (!strcmp(k, "down")) return PA_KEY_DOWN;
    if (!strcmp(k, "space")) return PA_KEY_SPACE;
    if (!strcmp(k, "enter")) return PA_KEY_ENTER;
    if (!strcmp(k, "esc")) return PA_KEY_ESC;
    return -1;
}

static int write_ppm(const char *path, const PA_Canvas *c) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    unsigned char *row = malloc((size_t)c->w * 3);
    for (int y = 0; y < c->h; y++) {
        const uint32_t *p = c->px + (size_t)y * c->w;
        for (int x = 0; x < c->w; x++) {
            row[x * 3 + 0] = (unsigned char)(p[x] >> 16);
            row[x * 3 + 1] = (unsigned char)(p[x] >> 8);
            row[x * 3 + 2] = (unsigned char)p[x];
        }
        fwrite(row, 1, (size_t)c->w * 3, f);
    }
    free(row);
    fclose(f);
    return 1;
}

void pa_set_landscape(int on) { (void)on; }
static int g_demo;
int pa_demo_mode(void) { return g_demo; }

int main(int argc, char **argv) {
    const char *game = "hub", *script = NULL, *shots = NULL, *out = "shot";
    int w = 540, h = 1170, autoplay = 0, bench = 0;
    double seconds = 0.0, hz = 60.0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--hz") && i + 1 < argc) hz = atof(argv[++i]);
        else if (!strcmp(argv[i], "--auto") && i + 1 < argc) autoplay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--demo") && i + 1 < argc) g_demo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bench")) bench = 1;
    }

    double shot_t[256];
    int nshots = 0;
    if (shots) {
        char *b = strdup(shots), *sv = NULL;
        for (char *p = strtok_r(b, ",", &sv); p && nshots < 256; p = strtok_r(NULL, ",", &sv)) shot_t[nshots++] = atof(p);
        free(b);
    }
    double last = 0.0;
    for (int i = 0; i < nshots; i++) if (shot_t[i] > last) last = shot_t[i];
    if (seconds < last + 1e-6) seconds = last;
    if (seconds <= 0.0) seconds = 1.0;
    if (!nshots) shot_t[nshots++] = seconds;

    pa_save_set_dir(".");
    if (!pa_canvas_init(&g_canvas, w, h)) return 1;
    pa_app_init(w, h);
    if (strcmp(game, "hub")) {
        int idx = (game[0] >= '0' && game[0] <= '9') ? atoi(game) : pa_app_debug_find(game);
        if (idx < 0) { fprintf(stderr, "unknown game '%s'\n", game); return 2; }
        pa_app_debug_launch(idx);
    }
    if (script) parse_script(script);

    PA_Input in;
    memset(&in, 0, sizeof(in));
    float key_until[PA_KEY_COUNT] = { 0 };
    double sim = 0.0, acc = 0.0, frame_dt = 1.0 / (hz < 1.0 ? 60.0 : hz);
    double next_auto = 0.18;
    int next_ev = 0, shot_i = 0;
    float press_x = 0, press_y = 0, prev_x = 0, prev_y = 0;
    int pend_press = 0, pend_release = 0, pend_tap = 0, pend_swipe = 0;

    double bench_total = 0.0, bench_max = 0.0;
    int bench_frames = 0;
    struct timespec t0, t1;
    while (shot_i < nshots) {
        if (bench) clock_gettime(CLOCK_MONOTONIC, &t0);
        /* Frame-level input, same classification the real platforms do. */
        acc += frame_dt;
        while (acc >= FIXED_STEP) {
            while (next_ev < g_nev && g_ev[next_ev].t <= sim + 1e-9) {
                Ev *e = &g_ev[next_ev++];
                if (!strcmp(e->verb, "tap")) {
                    in.x = e->x; in.y = e->y; press_x = e->x; press_y = e->y;
                    pend_press = 1; pend_release = 1; pend_tap = 1; in.down = 0;
                } else if (!strcmp(e->verb, "down")) {
                    in.x = e->x; in.y = e->y; press_x = e->x; press_y = e->y; in.down = 1; pend_press = 1;
                } else if (!strcmp(e->verb, "move")) {
                    in.x = e->x; in.y = e->y;
                } else if (!strcmp(e->verb, "up")) {
                    if (e->has_xy) { in.x = e->x; in.y = e->y; }
                    in.down = 0; pend_release = 1;
                    float dx = in.x - press_x, dy = in.y - press_y;
                    if (dx * dx + dy * dy > 30.0f * 30.0f)
                        pend_swipe = (dx * dx > dy * dy) ? (dx > 0 ? PA_SWIPE_RIGHT : PA_SWIPE_LEFT)
                                                         : (dy > 0 ? PA_SWIPE_DOWN : PA_SWIPE_UP);
                    else pend_tap = 1;
                } else if (!strcmp(e->verb, "swipe")) {
                    if (e->has_xy) { in.x = e->x; in.y = e->y; }
                    else { in.x = w * 0.5f; in.y = h * 0.5f; }
                    pend_swipe = !strcmp(e->a, "left") ? PA_SWIPE_LEFT : !strcmp(e->a, "right") ? PA_SWIPE_RIGHT
                               : !strcmp(e->a, "up") ? PA_SWIPE_UP : PA_SWIPE_DOWN;
                } else if (!strcmp(e->verb, "key")) {
                    int k = key_index(e->a);
                    if (k >= 0) { if (!in.keys[k]) in.key_pressed[k] = 1; in.keys[k] = 1; key_until[k] = (float)(sim + e->dur); }
                }
            }
            for (int k = 0; k < PA_KEY_COUNT; k++)
                if (in.keys[k] && sim >= key_until[k] && key_until[k] > 0) { in.keys[k] = 0; key_until[k] = 0; }

            if (autoplay == 4 || autoplay == 5) { in.keys[PA_KEY_RIGHT] = autoplay == 4; in.keys[PA_KEY_LEFT] = autoplay == 5; }
            if (autoplay && autoplay < 4 && sim >= next_auto) {
                next_auto += 0.18;
                pend_swipe = autoplay == 2 ? PA_SWIPE_LEFT : autoplay == 3 ? PA_SWIPE_RIGHT : PA_SWIPE_UP;
                in.x = w * 0.5f; in.y = h * 0.5f;
            }

            in.pressed = pend_press; in.released = pend_release;
            in.tapped = pend_tap; in.swipe = pend_tap ? PA_SWIPE_TAP : pend_swipe;
            if (pend_swipe) in.swipe = pend_swipe;
            in.dx = in.x - prev_x; in.dy = in.y - prev_y; prev_x = in.x; prev_y = in.y;
            pend_press = pend_release = pend_tap = pend_swipe = 0;

            pa_app_update((float)FIXED_STEP, &in);
            acc -= FIXED_STEP;
            sim += FIXED_STEP;
            in.pressed = in.released = in.tapped = 0;
            in.swipe = PA_SWIPE_NONE;
            in.wheel = 0;
            for (int k = 0; k < PA_KEY_COUNT; k++) in.key_pressed[k] = 0;
        }
        if (bench) {
            pa_app_render(&g_canvas);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
            if (sim > 1.0) { bench_total += ms; bench_frames++; if (ms > bench_max) bench_max = ms; }
        }
        if (sim + 1e-9 >= shot_t[shot_i]) {
            pa_app_render(&g_canvas);
            char path[1024];
            snprintf(path, sizeof(path), "%s_%02d.ppm", out, shot_i);
            if (!write_ppm(path, &g_canvas)) { fprintf(stderr, "write failed %s\n", path); return 1; }
            printf("%s t=%.2f\n", path, sim);
            shot_i++;
        }
    }
    if (bench && bench_frames)
        fprintf(stderr, "BENCH %s %dx%d frames=%d avg=%.2fms max=%.2fms\n", game, w, h, bench_frames,
                bench_total / bench_frames, bench_max);
    pa_app_shutdown();
    pa_canvas_free(&g_canvas);
    return 0;
}
