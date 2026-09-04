/* ===========================================================================
   POCKET ARCADE - audio
   A WinMM streaming mixer with a tiny synth on top. The browser build makes
   every sound from oscillators rather than samples; this does the same, which
   is why the native port needs no audio assets at all.
   =========================================================================== */
#include "pa.h"
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define BLOCK_FRAMES 512
#define BLOCK_COUNT 6
#define MAX_VOICES 16

typedef struct {
    int   active;
    int   shape;          /* 0 sine, 1 triangle, 2 square, 3 saw, 4 noise */
    float phase;
    float freq_from, freq_to;
    float gain;
    int   pos, length;    /* frames */
    uint32_t noise_state;
    float noise_low;
} Voice;

static HWAVEOUT   g_out;
static WAVEHDR    g_hdr[BLOCK_COUNT];
static short      g_buf[BLOCK_COUNT][BLOCK_FRAMES * 2];
static int        g_block;
static CRITICAL_SECTION g_lock;
static Voice      g_voices[MAX_VOICES];
static float      g_volume = 0.8f;
static HANDLE     g_thread;
static volatile LONG g_running;
static HANDLE     g_ready;

static float voice_sample(Voice *v) {
    float t = (float)v->pos / (float)v->length;
    if (t > 1.0f) t = 1.0f;

    /* Exponential frequency sweep, matching the browser's ramp. */
    float f = v->freq_from * powf(v->freq_to / v->freq_from, t);
    float out;

    if (v->shape == 4) {
        v->noise_state = v->noise_state * 1664525u + 1013904223u;
        float white = (float)((int)(v->noise_state >> 9) - 4194304) / 4194304.0f;
        /* One-pole lowpass; raw white noise reads as static rather than impact. */
        v->noise_low = v->noise_low * 0.72f + white * 0.28f;
        out = v->noise_low;
    } else {
        v->phase += f / (float)SAMPLE_RATE;
        if (v->phase >= 1.0f) v->phase -= floorf(v->phase);
        float p = v->phase;
        switch (v->shape) {
            case 1:  out = 4.0f * fabsf(p - 0.5f) - 1.0f; break;      /* triangle */
            case 2:  out = p < 0.5f ? 1.0f : -1.0f; break;            /* square   */
            case 3:  out = 2.0f * p - 1.0f; break;                    /* saw      */
            default: out = sinf(p * PA_TAU); break;                   /* sine     */
        }
    }

    /* Short attack, then a decay to silence. A hard cut would click. */
    float env;
    float attack = 0.02f;
    if (t < attack) env = t / attack;
    else env = powf(1.0f - (t - attack) / (1.0f - attack), 2.0f);

    return out * env * v->gain;
}

static void mix_block(short *dst, int frames) {
    memset(dst, 0, (size_t)frames * 2 * sizeof(short));

    EnterCriticalSection(&g_lock);
    for (int f = 0; f < frames; f++) {
        float acc = 0.0f;
        for (int i = 0; i < MAX_VOICES; i++) {
            Voice *v = &g_voices[i];
            if (!v->active) continue;
            acc += voice_sample(v);
            v->pos++;
            if (v->pos >= v->length) v->active = 0;
        }
        acc *= g_volume;
        if (acc > 1.0f) acc = 1.0f;
        if (acc < -1.0f) acc = -1.0f;
        short s = (short)(acc * 30000.0f);
        dst[f * 2] = s;
        dst[f * 2 + 1] = s;
    }
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI audio_thread(LPVOID unused) {
    (void)unused;
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        WAVEHDR *h = &g_hdr[g_block];
        if (h->dwFlags & WHDR_INQUEUE) { Sleep(1); continue; }
        mix_block(g_buf[g_block], BLOCK_FRAMES);
        waveOutWrite(g_out, h, sizeof(WAVEHDR));
        g_block = (g_block + 1) % BLOCK_COUNT;
    }
    return 0;
}

void pa_audio_init(void) {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    InitializeCriticalSection(&g_lock);
    memset(g_voices, 0, sizeof(g_voices));

    /* Audio is a nicety, never a blocker: if the device will not open, every
       sound call below quietly does nothing and the game still runs. */
    if (waveOutOpen(&g_out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_out = NULL;
        return;
    }

    for (int i = 0; i < BLOCK_COUNT; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR));
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = BLOCK_FRAMES * 2 * sizeof(short);
        waveOutPrepareHeader(g_out, &g_hdr[i], sizeof(WAVEHDR));
    }

    g_ready = NULL;
    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

void pa_audio_shutdown(void) {
    if (!g_out) return;
    InterlockedExchange(&g_running, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 500);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    waveOutReset(g_out);
    for (int i = 0; i < BLOCK_COUNT; i++) waveOutUnprepareHeader(g_out, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_out);
    g_out = NULL;
    DeleteCriticalSection(&g_lock);
}

void pa_audio_set_volume(float v) { g_volume = pa_clamp01(v); }

static void push_voice(int shape, float from, float to, float seconds, float gain) {
    if (!g_out || g_volume <= 0.0f) return;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (g_voices[i].active) continue;
        Voice *v = &g_voices[i];
        v->active = 1;
        v->shape = shape;
        v->phase = 0.0f;
        v->freq_from = from < 20.0f ? 20.0f : from;
        v->freq_to = to < 20.0f ? 20.0f : to;
        v->gain = gain;
        v->pos = 0;
        v->length = (int)(seconds * SAMPLE_RATE);
        if (v->length < 32) v->length = 32;
        v->noise_state = 0x9E3779B9u ^ (uint32_t)i;
        v->noise_low = 0.0f;
        break;
    }
    LeaveCriticalSection(&g_lock);
}

void pa_tone(float from_hz, float to_hz, float seconds, int shape, float gain) {
    push_voice(shape, from_hz, to_hz, seconds, gain);
}

void pa_noise(float seconds, float gain) {
    push_voice(4, 200.0f, 200.0f, seconds, gain);
}

void pa_sfx(const char *name) {
    if (!name) return;
    if      (!strcmp(name, "select"))  pa_tone(660, 720, 0.06f, 0, 0.09f);
    else if (!strcmp(name, "good"))    pa_tone(520, 1180, 0.16f, 1, 0.14f);
    else if (!strcmp(name, "bad"))     pa_tone(420, 150, 0.22f, 3, 0.12f);
    else if (!strcmp(name, "coin"))    pa_tone(880, 1760, 0.10f, 1, 0.11f);
    else if (!strcmp(name, "hop"))     pa_tone(560, 900, 0.06f, 2, 0.06f);
    else if (!strcmp(name, "pop"))     pa_tone(700, 1300, 0.07f, 0, 0.09f);
    else if (!strcmp(name, "thud"))    pa_tone(180, 90, 0.09f, 0, 0.09f);
    else if (!strcmp(name, "hit"))     pa_noise(0.14f, 0.20f);
    else if (!strcmp(name, "boom"))  { pa_noise(0.30f, 0.24f); pa_tone(160, 48, 0.30f, 3, 0.14f); }
    else if (!strcmp(name, "levelup")) { pa_tone(523, 523, 0.13f, 1, 0.12f);
                                         pa_tone(784, 784, 0.13f, 1, 0.10f);
                                         pa_tone(1046, 1046, 0.16f, 1, 0.09f); }
    else if (!strcmp(name, "win"))   { pa_tone(523, 523, 0.14f, 1, 0.12f);
                                        pa_tone(659, 659, 0.14f, 1, 0.11f);
                                        pa_tone(784, 784, 0.14f, 1, 0.10f);
                                        pa_tone(1047, 1047, 0.20f, 1, 0.10f); }
    else if (!strcmp(name, "lose"))  { pa_tone(440, 262, 0.34f, 3, 0.11f); }
}
