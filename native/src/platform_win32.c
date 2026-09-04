/* ===========================================================================
   POCKET ARCADE - Win32 platform layer
   Creates the window, owns the frame clock and the input state, and blits the
   software framebuffer with StretchDIBits. There is no browser, no embedded
   webview and no scripting runtime anywhere in this binary.
   =========================================================================== */
#include "pa.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GET_X_LPARAM sign-extends; the plain LOWORD does not, and a pointer dragged
   past the left edge of the window then wraps to 65000 instead of going
   negative. */
#define GET_X_LPARAM_SAFE(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM_SAFE(lp) ((int)(short)HIWORD(lp))

static PA_Canvas   g_canvas;
static PA_Input    g_input;
static BITMAPINFO  g_bmi;
static HWND        g_hwnd;
static int         g_quit;
static int         g_client_w = 540, g_client_h = 960;

/* Pointer bookkeeping for the swipe/tap classification. */
static float g_press_x, g_press_y;
static double g_press_time;
static int    g_pending_press, g_pending_release;
static float  g_prev_x, g_prev_y;

/* One simulation step. 120Hz rather than 60 so the fixed cadence is a clean
   divisor of the common refresh rates and input latency stays low. */
#define FIXED_STEP (1.0 / 120.0)
#define MAX_STEPS  8

static double accumulator;

static double now_seconds(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static void set_key(WPARAM vk, int down) {
    int idx = -1;
    switch (vk) {
        case VK_LEFT:  case 'A': idx = PA_KEY_LEFT;  break;
        case VK_RIGHT: case 'D': idx = PA_KEY_RIGHT; break;
        case VK_UP:    case 'W': idx = PA_KEY_UP;    break;
        case VK_DOWN:  case 'S': idx = PA_KEY_DOWN;  break;
        case VK_SPACE: idx = PA_KEY_SPACE; break;
        case VK_ESCAPE: idx = PA_KEY_ESC;  break;
        case VK_RETURN: idx = PA_KEY_ENTER; break;
        default: return;
    }
    if (down && !g_input.keys[idx]) g_input.key_pressed[idx] = 1;
    g_input.keys[idx] = down;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            g_quit = 1;
            PostQuitMessage(0);
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0) {
                g_client_w = w;
                g_client_h = h;
                pa_canvas_resize(&g_canvas, w, h);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            /* The frame paints every pixel, so letting Windows clear first only
               buys a flash of white on resize. */
            return 1;

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            g_input.x = (float)GET_X_LPARAM_SAFE(lp);
            g_input.y = (float)GET_Y_LPARAM_SAFE(lp);
            g_press_x = g_input.x;
            g_press_y = g_input.y;
            g_press_time = now_seconds();
            g_pending_press = 1;
            g_input.down = 1;
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            g_pending_release = 1;
            g_input.down = 0;
            return 0;

        case WM_MOUSEMOVE:
            g_input.x = (float)GET_X_LPARAM_SAFE(lp);
            g_input.y = (float)GET_Y_LPARAM_SAFE(lp);
            return 0;

        case WM_KEYDOWN: set_key(wp, 1); return 0;
        case WM_KEYUP:   set_key(wp, 0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void begin_frame(void) {
    g_input.dx = g_input.x - g_prev_x;
    g_input.dy = g_input.y - g_prev_y;
    g_prev_x = g_input.x;
    g_prev_y = g_input.y;

    g_input.pressed = g_pending_press;
    g_input.released = g_pending_release;
    g_input.tapped = 0;
    g_input.swipe = PA_SWIPE_NONE;

    if (g_pending_release) {
        float dx = g_input.x - g_press_x;
        float dy = g_input.y - g_press_y;
        float dist = dx * dx + dy * dy;
        if (dist > 26.0f * 26.0f) {
            if (dx * dx > dy * dy) g_input.swipe = dx > 0 ? PA_SWIPE_RIGHT : PA_SWIPE_LEFT;
            else                   g_input.swipe = dy > 0 ? PA_SWIPE_DOWN : PA_SWIPE_UP;
        } else if (now_seconds() - g_press_time < 0.45) {
            g_input.swipe = PA_SWIPE_TAP;
            g_input.tapped = 1;
        }
    }

    g_pending_press = 0;
    g_pending_release = 0;
}

static void end_frame(void) {
    for (int i = 0; i < PA_KEY_COUNT; i++) g_input.key_pressed[i] = 0;
}

static void present(HDC dc) {
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = g_canvas.w;
    /* Negative height gives a top-down DIB, matching the framebuffer's layout
       and saving a per-frame vertical flip. */
    g_bmi.bmiHeader.biHeight = -g_canvas.h;
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc,
        0, 0, g_client_w, g_client_h,
        0, 0, g_canvas.w, g_canvas.h,
        g_canvas.px, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
}

/* Headless capture: --shot <path> <seconds> renders without a message pump and
   writes a BMP. It is how the build is verified from a Linux CI box, where
   there is no way to look at a window. */
static int write_bmp(const char *path, const PA_Canvas *c) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    int row = c->w * 3;
    int pad = (4 - (row % 4)) % 4;
    int data = (row + pad) * c->h;
    int size = 54 + data;

    unsigned char header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    memcpy(header + 2, &size, 4);
    int offset = 54;
    memcpy(header + 10, &offset, 4);
    int hdr = 40;
    memcpy(header + 14, &hdr, 4);
    memcpy(header + 18, &c->w, 4);
    int flipped = c->h;
    memcpy(header + 22, &flipped, 4);
    short planes = 1, bits = 24;
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bits, 2);
    memcpy(header + 34, &data, 4);
    fwrite(header, 1, sizeof(header), f);

    /*
     * One buffered write per row. Emitting three bytes at a time meant half a
     * million stdio calls for a single frame, which is both slow and - through
     * a translation layer - a good way to end up with a partially written file.
     */
    unsigned char *line = (unsigned char *)malloc((size_t)(row + pad));
    if (!line) { fclose(f); return 0; }
    memset(line, 0, (size_t)(row + pad));

    int ok = 1;
    for (int y = c->h - 1; y >= 0 && ok; y--) {
        const uint32_t *src = c->px + (size_t)y * (size_t)c->w;
        for (int x = 0; x < c->w; x++) {
            line[x * 3 + 0] = (unsigned char)(src[x] & 0xFF);
            line[x * 3 + 1] = (unsigned char)((src[x] >> 8) & 0xFF);
            line[x * 3 + 2] = (unsigned char)((src[x] >> 16) & 0xFF);
        }
        if (fwrite(line, 1, (size_t)(row + pad), f) != (size_t)(row + pad)) ok = 0;
    }

    free(line);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

void pa_app_debug_launch(int index);

static int run_headless(const char *path, double seconds, int play, int autoplay, double hz) {
    if (!pa_canvas_init(&g_canvas, g_client_w, g_client_h)) return 1;
    pa_app_init(g_canvas.w, g_canvas.h);
    if (play >= 0) pa_app_debug_launch(play);

    memset(&g_input, 0, sizeof(g_input));
    if (hz < 1.0) hz = 60.0;
    if (seconds <= 0.0) seconds = 1.0;

    int frames = (int)(seconds * hz);
    if (frames < 1) frames = 1;
    double frame_dt = 1.0 / hz;

    /*
     * The capture runs the same accumulator the real loop does, driven at
     * whatever refresh rate is asked for. Two runs of the same wall-clock
     * duration at different rates must produce the same simulation, and
     * comparing their output is how that gets checked.
     */
    double sim_time = 0.0;
    double next_input = 0.18;
    int total_steps = 0;
    double start = now_seconds();

    for (int i = 0; i < frames; i++) {
        begin_frame();

        accumulator += frame_dt;
        if (accumulator > 0.25) accumulator = 0.25;
        int steps = 0;
        while (accumulator >= FIXED_STEP && steps < MAX_STEPS) {
            /*
             * Synthetic input is scheduled per simulation step, not per frame.
             * Injecting it per frame hands a 240Hz run four times as many
             * chances to fire and the two runs diverge for reasons that have
             * nothing to do with the loop being correct - which is exactly the
             * thing under test.
             */
            if (autoplay && sim_time >= next_input) {
                next_input += 0.18;
                g_input.swipe = (autoplay == 2) ? PA_SWIPE_LEFT
                              : (autoplay == 3) ? PA_SWIPE_RIGHT : PA_SWIPE_UP;
                /* Park the synthetic pointer mid-screen. At the origin it sits
                   inside the hub's MENU button, and a swipe there walks
                   straight out of the game being captured. */
                g_input.x = (float)g_canvas.w * 0.5f;
                g_input.y = (float)g_canvas.h * 0.5f;
            }
            pa_app_update((float)FIXED_STEP, &g_input);
            accumulator -= FIXED_STEP;
            sim_time += FIXED_STEP;
            steps++;
            total_steps++;
            g_input.pressed = 0;
            g_input.released = 0;
            g_input.tapped = 0;
            g_input.swipe = PA_SWIPE_NONE;
            for (int k = 0; k < PA_KEY_COUNT; k++) g_input.key_pressed[k] = 0;
        }
        if (steps >= MAX_STEPS) accumulator = 0.0;

        pa_app_render(&g_canvas);
        end_frame();
    }
    double elapsed = now_seconds() - start;

    int ok = write_bmp(path, &g_canvas);
    printf("%.0fHz: %d frames, %d sim steps, %.3fs simulated in %.3fs wall -> %s\n",
           hz, frames, total_steps, sim_time, elapsed, path);

    pa_app_shutdown();
    pa_canvas_free(&g_canvas);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    int play = -1, autoplay = 0;
    double hz = 60.0, seconds = 0.0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--play") && i + 1 < argc) play = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--auto") && i + 1 < argc) autoplay = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--hz") && i + 1 < argc) hz = atof(argv[i + 1]);
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[i + 1]);
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            const char *path = argv[i + 1];
            if (seconds <= 0.0) {
                /* Legacy positional form: --shot <path> <frames>. */
                int frames = (i + 2 < argc && argv[i + 2][0] != '-') ? atoi(argv[i + 2]) : 60;
                seconds = (double)frames / 60.0;
            }
            return run_headless(path, seconds, play, autoplay, hz);
        }
        if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            g_client_w = atoi(argv[i + 1]);
            g_client_h = atoi(argv[i + 2]);
            i += 2;
        }
    }

    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); /* IDC_ARROW, wide */
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"PocketArcadeWindow";
    if (!RegisterClassExW(&wc)) return 1;

    RECT r = { 0, 0, g_client_w, g_client_h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Pocket Arcade",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;

    if (!pa_canvas_init(&g_canvas, g_client_w, g_client_h)) return 1;
    pa_audio_init();
    pa_app_init(g_canvas.w, g_canvas.h);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    HDC dc = GetDC(g_hwnd);
    /* Ask for 1ms timer resolution; without it Sleep rounds up to the system
       tick and the loop stutters at around 64 frames a second. */
    timeBeginPeriod(1);
    double previous = now_seconds();

    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_quit = 1; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit) break;

        double current = now_seconds();
        double frame = current - previous;
        previous = current;
        /* Clamped so a stall never teleports anything through a wall, and so a
           long pause cannot queue up hundreds of catch-up steps. */
        if (frame > 0.25) frame = 0.25;
        if (frame < 0.0) frame = 0.0;

        begin_frame();

        /*
         * Fixed timestep. The simulation always advances in FIXED_STEP chunks
         * regardless of how often the display refreshes: on a 240Hz panel the
         * loop simply renders four times per simulated step instead of running
         * the game four times as fast. Passing raw frame deltas straight into
         * the games looked correct in the arithmetic but was not - anything
         * with per-frame damping, an integer counter, or an event that fires on
         * a threshold behaves differently at a different cadence, and the whole
         * suite ran fast on a high refresh rate monitor.
         */
        accumulator += frame;
        int steps = 0;
        while (accumulator >= FIXED_STEP && steps < MAX_STEPS) {
            pa_app_update((float)FIXED_STEP, &g_input);
            accumulator -= FIXED_STEP;
            steps++;
            /* Edge-triggered input belongs to exactly one step. Leaving it set
               would fire a tap or a swipe once per catch-up step. */
            g_input.pressed = 0;
            g_input.released = 0;
            g_input.tapped = 0;
            g_input.swipe = PA_SWIPE_NONE;
            for (int k = 0; k < PA_KEY_COUNT; k++) g_input.key_pressed[k] = 0;
        }
        /* If we hit the step ceiling the machine cannot keep up; drop the debt
           rather than accumulating it into a death spiral. */
        if (steps >= MAX_STEPS) accumulator = 0.0;

        end_frame();

        pa_app_render(&g_canvas);
        present(dc);

        if (pa_app_should_quit()) g_quit = 1;

        /* Yield rather than spin: this is a 2D arcade, not a benchmark. The
           sleep only caps how often we redraw; it has no say in simulation
           speed any more. */
        double spent = now_seconds() - current;
        if (spent < FIXED_STEP) Sleep((DWORD)((FIXED_STEP - spent) * 1000.0));
    }

    timeEndPeriod(1);
    ReleaseDC(g_hwnd, dc);
    pa_app_shutdown();
    pa_audio_shutdown();
    pa_canvas_free(&g_canvas);
    return 0;
}
