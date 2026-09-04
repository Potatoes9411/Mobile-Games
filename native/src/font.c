/* ===========================================================================
   POCKET ARCADE - stroke font
   Glyphs are polylines on a 0..1 box, drawn with the rasterizer's stroke path.
   A stroke font stays crisp at any size and ships as a few hundred bytes of
   coordinates, where a bitmap atlas would need one baked size per use and blur
   the moment the UI scaled.
   =========================================================================== */
#include "pa.h"
#include <string.h>
#include <math.h>

/* Each glyph is a list of strokes; each stroke is a run of points, terminated
   by the sentinel -1. Coordinates are in a 0..1 box, y down. */
#define E -1.0f

static const float G_A[] = { 0.05f,1.0f, 0.5f,0.0f, 0.95f,1.0f, E, 0.22f,0.64f, 0.78f,0.64f, E, E };
static const float G_B[] = { 0.12f,1.0f, 0.12f,0.0f, 0.66f,0.0f, 0.86f,0.16f, 0.86f,0.34f, 0.66f,0.48f, 0.12f,0.48f, E,
                             0.66f,0.48f, 0.90f,0.66f, 0.90f,0.84f, 0.68f,1.0f, 0.12f,1.0f, E, E };
static const float G_C[] = { 0.92f,0.20f, 0.66f,0.0f, 0.34f,0.0f, 0.08f,0.22f, 0.08f,0.78f, 0.34f,1.0f, 0.66f,1.0f, 0.92f,0.80f, E, E };
static const float G_D[] = { 0.12f,1.0f, 0.12f,0.0f, 0.58f,0.0f, 0.88f,0.24f, 0.88f,0.76f, 0.58f,1.0f, 0.12f,1.0f, E, E };
static const float G_E[] = { 0.90f,0.0f, 0.12f,0.0f, 0.12f,1.0f, 0.90f,1.0f, E, 0.12f,0.50f, 0.72f,0.50f, E, E };
static const float G_F[] = { 0.90f,0.0f, 0.12f,0.0f, 0.12f,1.0f, E, 0.12f,0.50f, 0.72f,0.50f, E, E };
static const float G_G[] = { 0.92f,0.20f, 0.66f,0.0f, 0.34f,0.0f, 0.08f,0.22f, 0.08f,0.78f, 0.34f,1.0f, 0.68f,1.0f, 0.92f,0.80f, 0.92f,0.54f, 0.56f,0.54f, E, E };
static const float G_H[] = { 0.12f,0.0f, 0.12f,1.0f, E, 0.88f,0.0f, 0.88f,1.0f, E, 0.12f,0.50f, 0.88f,0.50f, E, E };
static const float G_I[] = { 0.50f,0.0f, 0.50f,1.0f, E, E };
static const float G_J[] = { 0.86f,0.0f, 0.86f,0.76f, 0.62f,1.0f, 0.30f,1.0f, 0.10f,0.80f, E, E };
static const float G_K[] = { 0.12f,0.0f, 0.12f,1.0f, E, 0.88f,0.0f, 0.12f,0.54f, E, 0.38f,0.38f, 0.90f,1.0f, E, E };
static const float G_L[] = { 0.14f,0.0f, 0.14f,1.0f, 0.88f,1.0f, E, E };
static const float G_M[] = { 0.08f,1.0f, 0.08f,0.0f, 0.50f,0.56f, 0.92f,0.0f, 0.92f,1.0f, E, E };
static const float G_N[] = { 0.12f,1.0f, 0.12f,0.0f, 0.88f,1.0f, 0.88f,0.0f, E, E };
static const float G_O[] = { 0.34f,0.0f, 0.66f,0.0f, 0.90f,0.22f, 0.90f,0.78f, 0.66f,1.0f, 0.34f,1.0f, 0.10f,0.78f, 0.10f,0.22f, 0.34f,0.0f, E, E };
static const float G_P[] = { 0.12f,1.0f, 0.12f,0.0f, 0.68f,0.0f, 0.90f,0.20f, 0.90f,0.38f, 0.68f,0.56f, 0.12f,0.56f, E, E };
static const float G_Q[] = { 0.34f,0.0f, 0.66f,0.0f, 0.90f,0.22f, 0.90f,0.78f, 0.66f,1.0f, 0.34f,1.0f, 0.10f,0.78f, 0.10f,0.22f, 0.34f,0.0f, E,
                             0.60f,0.72f, 0.96f,1.05f, E, E };
static const float G_R[] = { 0.12f,1.0f, 0.12f,0.0f, 0.68f,0.0f, 0.90f,0.20f, 0.90f,0.38f, 0.68f,0.56f, 0.12f,0.56f, E,
                             0.50f,0.56f, 0.92f,1.0f, E, E };
static const float G_S[] = { 0.90f,0.16f, 0.62f,0.0f, 0.32f,0.0f, 0.10f,0.18f, 0.12f,0.40f, 0.40f,0.50f, 0.66f,0.56f, 0.90f,0.70f, 0.88f,0.86f, 0.62f,1.0f, 0.30f,1.0f, 0.08f,0.84f, E, E };
static const float G_T[] = { 0.06f,0.0f, 0.94f,0.0f, E, 0.50f,0.0f, 0.50f,1.0f, E, E };
static const float G_U[] = { 0.12f,0.0f, 0.12f,0.76f, 0.36f,1.0f, 0.64f,1.0f, 0.88f,0.76f, 0.88f,0.0f, E, E };
static const float G_V[] = { 0.06f,0.0f, 0.50f,1.0f, 0.94f,0.0f, E, E };
static const float G_W[] = { 0.04f,0.0f, 0.26f,1.0f, 0.50f,0.36f, 0.74f,1.0f, 0.96f,0.0f, E, E };
static const float G_X[] = { 0.08f,0.0f, 0.92f,1.0f, E, 0.92f,0.0f, 0.08f,1.0f, E, E };
static const float G_Y[] = { 0.08f,0.0f, 0.50f,0.50f, 0.92f,0.0f, E, 0.50f,0.50f, 0.50f,1.0f, E, E };
static const float G_Z[] = { 0.08f,0.0f, 0.92f,0.0f, 0.10f,1.0f, 0.92f,1.0f, E, E };

static const float G_0[] = { 0.34f,0.0f, 0.66f,0.0f, 0.90f,0.22f, 0.90f,0.78f, 0.66f,1.0f, 0.34f,1.0f, 0.10f,0.78f, 0.10f,0.22f, 0.34f,0.0f, E,
                             0.78f,0.22f, 0.24f,0.80f, E, E };
static const float G_1[] = { 0.26f,0.20f, 0.54f,0.0f, 0.54f,1.0f, E, 0.24f,1.0f, 0.84f,1.0f, E, E };
static const float G_2[] = { 0.10f,0.22f, 0.34f,0.0f, 0.66f,0.0f, 0.90f,0.20f, 0.90f,0.38f, 0.10f,1.0f, 0.92f,1.0f, E, E };
static const float G_3[] = { 0.10f,0.14f, 0.36f,0.0f, 0.70f,0.0f, 0.90f,0.18f, 0.86f,0.40f, 0.50f,0.50f, E,
                             0.50f,0.50f, 0.88f,0.60f, 0.92f,0.82f, 0.68f,1.0f, 0.34f,1.0f, 0.10f,0.86f, E, E };
static const float G_4[] = { 0.72f,1.0f, 0.72f,0.0f, 0.08f,0.70f, 0.94f,0.70f, E, E };
static const float G_5[] = { 0.90f,0.0f, 0.20f,0.0f, 0.14f,0.44f, 0.56f,0.40f, 0.90f,0.58f, 0.88f,0.84f, 0.62f,1.0f, 0.28f,1.0f, 0.08f,0.86f, E, E };
static const float G_6[] = { 0.86f,0.10f, 0.56f,0.0f, 0.28f,0.10f, 0.12f,0.44f, 0.12f,0.80f, 0.36f,1.0f, 0.64f,1.0f, 0.88f,0.82f, 0.88f,0.64f, 0.62f,0.48f, 0.30f,0.50f, 0.12f,0.66f, E, E };
static const float G_7[] = { 0.08f,0.0f, 0.92f,0.0f, 0.44f,1.0f, E, E };
static const float G_8[] = { 0.38f,0.50f, 0.16f,0.34f, 0.18f,0.12f, 0.44f,0.0f, 0.62f,0.0f, 0.86f,0.14f, 0.84f,0.36f, 0.60f,0.50f, 0.32f,0.58f, 0.12f,0.76f, 0.16f,0.92f, 0.42f,1.0f, 0.62f,1.0f, 0.88f,0.90f, 0.88f,0.70f, 0.60f,0.50f, E, E };
static const float G_9[] = { 0.14f,0.90f, 0.44f,1.0f, 0.72f,0.90f, 0.88f,0.56f, 0.88f,0.20f, 0.64f,0.0f, 0.36f,0.0f, 0.12f,0.18f, 0.12f,0.36f, 0.38f,0.52f, 0.70f,0.50f, 0.88f,0.34f, E, E };

static const float G_DOT[]   = { 0.44f,0.94f, 0.56f,0.94f, E, E };
static const float G_COMMA[] = { 0.56f,0.90f, 0.40f,1.14f, E, E };
static const float G_COLON[] = { 0.46f,0.34f, 0.54f,0.34f, E, 0.46f,0.86f, 0.54f,0.86f, E, E };
static const float G_DASH[]  = { 0.16f,0.52f, 0.84f,0.52f, E, E };
static const float G_PLUS[]  = { 0.16f,0.52f, 0.84f,0.52f, E, 0.50f,0.18f, 0.50f,0.86f, E, E };
static const float G_SLASH[] = { 0.86f,0.0f, 0.14f,1.0f, E, E };
static const float G_EXCL[]  = { 0.50f,0.0f, 0.50f,0.66f, E, 0.50f,0.92f, 0.50f,0.98f, E, E };
static const float G_QUERY[] = { 0.14f,0.22f, 0.36f,0.0f, 0.66f,0.0f, 0.88f,0.20f, 0.84f,0.42f, 0.50f,0.58f, 0.50f,0.70f, E, 0.50f,0.92f, 0.50f,0.98f, E, E };
static const float G_MID[]   = { 0.44f,0.48f, 0.56f,0.48f, E, E };
static const float G_LT[]    = { 0.74f,0.14f, 0.26f,0.50f, 0.74f,0.86f, E, E };
static const float G_GT[]    = { 0.26f,0.14f, 0.74f,0.50f, 0.26f,0.86f, E, E };
static const float G_EMPTY[] = { E };

static const float *glyph_for(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
    switch (ch) {
        case 'A': return G_A; case 'B': return G_B; case 'C': return G_C; case 'D': return G_D;
        case 'E': return G_E; case 'F': return G_F; case 'G': return G_G; case 'H': return G_H;
        case 'I': return G_I; case 'J': return G_J; case 'K': return G_K; case 'L': return G_L;
        case 'M': return G_M; case 'N': return G_N; case 'O': return G_O; case 'P': return G_P;
        case 'Q': return G_Q; case 'R': return G_R; case 'S': return G_S; case 'T': return G_T;
        case 'U': return G_U; case 'V': return G_V; case 'W': return G_W; case 'X': return G_X;
        case 'Y': return G_Y; case 'Z': return G_Z;
        case '0': return G_0; case '1': return G_1; case '2': return G_2; case '3': return G_3;
        case '4': return G_4; case '5': return G_5; case '6': return G_6; case '7': return G_7;
        case '8': return G_8; case '9': return G_9;
        case '.': return G_DOT;   case ',': return G_COMMA; case ':': return G_COLON;
        case '-': return G_DASH;  case '+': return G_PLUS;  case '/': return G_SLASH;
        case '!': return G_EXCL;  case '?': return G_QUERY;
        case '<': return G_LT;    case '>': return G_GT;
        /* The interpunct the UI uses as a separator, in whichever encoding
           arrives - the sources are UTF-8 but a byte-oriented walk sees the
           lead byte alone. */
        case 0xB7: case 0xC2: return G_MID;
        default: return G_EMPTY;
    }
}

/* Advance is uniform: the UI is set in caps at wide tracking, where a
   proportional metric buys nothing and costs alignment. */
#define GLYPH_ADVANCE 0.72f

float pa_text_width(const char *text, float size, float tracking) {
    int n = 0;
    for (const char *p = text; *p; p++) {
        if ((unsigned char)*p == 0xC2) continue;   /* UTF-8 lead byte, drawn once */
        n++;
    }
    if (n == 0) return 0.0f;
    return (float)n * (size * GLYPH_ADVANCE + tracking) - tracking;
}

void pa_text(PA_Canvas *c, const char *text, float x, float y, float size,
             PA_Color col, PA_Align align, float tracking) {
    float total = pa_text_width(text, size, tracking);
    float pen = x;
    if (align == PA_ALIGN_CENTER) pen = x - total * 0.5f;
    else if (align == PA_ALIGN_RIGHT) pen = x - total;

    float weight = size * 0.13f;
    if (weight < 1.2f) weight = 1.2f;

    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == 0xC2) continue;
        if (ch == ' ') { pen += size * GLYPH_ADVANCE + tracking; continue; }

        const float *g = glyph_for(ch);
        int i = 0;
        while (g[i] != E) {
            PA_Vec2 pts[32];
            int n = 0;
            while (g[i] != E && n < 32) {
                pts[n].x = pen + g[i] * size * 0.66f;
                pts[n].y = y + g[i + 1] * size;
                n++;
                i += 2;
            }
            i++;                       /* step over the stroke terminator */
            if (n == 1) pa_fill_circle(c, pts[0].x, pts[0].y, weight * 0.5f, col);
            else if (n > 1) pa_stroke_poly(c, pts, n, 0, weight, col);
            if (g[i] == E) break;      /* the second sentinel ends the glyph */
        }
        pen += size * GLYPH_ADVANCE + tracking;
    }
}
