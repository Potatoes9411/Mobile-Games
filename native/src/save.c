/* ===========================================================================
   POCKET ARCADE - save file
   One flat "key=value" text file beside the executable. Deliberately not a
   binary blob: a save that gets truncated, hand edited or copied between builds
   degrades to defaults instead of to a crash, and it can be read by a human
   when a score looks wrong.
   =========================================================================== */
#include "pa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_ENTRIES 64
#define MAX_KEY 32

typedef struct { char key[MAX_KEY]; int value; } Entry;

static Entry g_entries[MAX_ENTRIES];
static int   g_count;
static int   g_dirty;
static char  g_path[1024];
static int   g_loaded;

static void resolve_path(void) {
    if (g_path[0]) return;
#ifdef _WIN32
    /* Beside the exe, not in the working directory: a shortcut can launch the
       game from anywhere and the save should still be found. */
    extern unsigned long __stdcall GetModuleFileNameA(void *, char *, unsigned long);
    char exe[1024];
    unsigned long n = GetModuleFileNameA(0, exe, sizeof(exe) - 1);
    if (n > 0 && n < sizeof(exe)) {
        exe[n] = 0;
        char *slash = strrchr(exe, '\\');
        if (slash) {
            *slash = 0;
            snprintf(g_path, sizeof(g_path), "%s\\pocket-arcade.save", exe);
            return;
        }
    }
#endif
    snprintf(g_path, sizeof(g_path), "pocket-arcade.save");
}

static Entry *find(const char *key) {
    for (int i = 0; i < g_count; i++) {
        if (!strcmp(g_entries[i].key, key)) return &g_entries[i];
    }
    return NULL;
}

void pa_save_load(void) {
    if (g_loaded) return;
    g_loaded = 1;
    resolve_path();

    FILE *f = fopen(g_path, "r");
    if (!f) return;

    char line[128];
    while (g_count < MAX_ENTRIES && fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;

        /* Trim whitespace off the key rather than rejecting the line: a save
           edited by hand is a supported way to reset a score. */
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        size_t len = strlen(k);
        while (len > 0 && (k[len - 1] == ' ' || k[len - 1] == '\t')) k[--len] = 0;
        if (len == 0 || len >= MAX_KEY) continue;

        Entry *e = &g_entries[g_count++];
        memcpy(e->key, k, len + 1);
        e->value = atoi(eq + 1);
    }
    fclose(f);
}

void pa_save_flush(void) {
    if (!g_dirty) return;
    resolve_path();

    FILE *f = fopen(g_path, "w");
    if (!f) { g_dirty = 0; return; }   /* read-only install: play on regardless */
    for (int i = 0; i < g_count; i++) {
        fprintf(f, "%s=%d\n", g_entries[i].key, g_entries[i].value);
    }
    fclose(f);
    g_dirty = 0;
}

int pa_save_get(const char *key, int fallback) {
    pa_save_load();
    Entry *e = find(key);
    return e ? e->value : fallback;
}

void pa_save_set(const char *key, int value) {
    pa_save_load();
    Entry *e = find(key);
    if (e) {
        if (e->value == value) return;
        e->value = value;
        g_dirty = 1;
        return;
    }
    if (g_count >= MAX_ENTRIES) return;
    size_t len = strlen(key);
    if (len >= MAX_KEY) return;
    e = &g_entries[g_count++];
    memcpy(e->key, key, len + 1);
    e->value = value;
    g_dirty = 1;
}
