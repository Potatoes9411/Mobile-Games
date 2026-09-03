/*
 * Pocket Arcade - Windows launcher
 *
 * A ~100 KB native Win32 executable with the entire game embedded inside it.
 * On launch it unpacks the game next to the user's app data and opens it in a
 * chrome-less browser window (Edge or Chrome in --app mode), which on Windows 11
 * looks and behaves like a native application window: no tabs, no address bar,
 * its own taskbar entry. If neither browser is found it falls back to the
 * default browser so the game always starts.
 *
 * Build:  desktop/win-launcher/build.sh   (mingw-w64 cross compiler)
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#include "game_data.h"

#define APP_DIR    L"PocketArcade"
#define GAME_FILE  L"game.html"
#define WINDOW_ARG L"--window-size=560,980"

/*
 * mingw-w64 maps bare swprintf() to the non-conforming MSVC variant, which takes
 * no buffer-size argument - passing one shifts every following argument and the
 * program wanders off before it does any work. _vsnwprintf has a stable
 * signature across toolchains, so all string building goes through this helper.
 */
static void FormatW(wchar_t *out, size_t cap, const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(out, cap - 1, fmt, args);
    va_end(args);
    out[cap - 1] = L'\0';
}

static BOOL FileExistsW(const wchar_t *path)
{
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/* Writes the embedded HTML to disk, overwriting any previous version. */
static BOOL WriteGame(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written = 0;
    BOOL ok = WriteFile(file, GAME_HTML, (DWORD)GAME_HTML_LEN, &written, NULL);
    CloseHandle(file);

    return ok && written == (DWORD)GAME_HTML_LEN;
}

/* Turns C:\dir\game.html into file:///C:/dir/game.html */
static void ToFileUrl(const wchar_t *path, wchar_t *out, size_t outLen)
{
    FormatW(out, outLen, L"file:///%s", path);
    for (size_t i = 0; out[i]; i++) {
        if (out[i] == L'\\') out[i] = L'/';
    }
}

static BOOL TryLaunch(const wchar_t *browser, const wchar_t *url, const wchar_t *profile)
{
    if (!FileExistsW(browser)) return FALSE;

    wchar_t command[4096];
    FormatW(command, 4096,
             L"\"%s\" --app=\"%s\" %s --user-data-dir=\"%s\" "
             L"--allow-file-access-from-files --no-first-run --no-default-browser-check",
             browser, url, WINDOW_ARG, profile);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return FALSE;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

int main(void)
{
    wchar_t base[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) {
        if (!GetEnvironmentVariableW(L"TEMP", base, MAX_PATH)) {
            MessageBoxW(NULL, L"Could not locate a writable folder.",
                        L"Pocket Arcade", MB_ICONERROR | MB_OK);
            return 1;
        }
    }

    wchar_t appDir[MAX_PATH];
    FormatW(appDir, MAX_PATH, L"%s\\%s", base, APP_DIR);
    CreateDirectoryW(appDir, NULL);

    wchar_t profile[MAX_PATH];
    FormatW(profile, MAX_PATH, L"%s\\profile", appDir);
    CreateDirectoryW(profile, NULL);

    wchar_t gamePath[MAX_PATH];
    FormatW(gamePath, MAX_PATH, L"%s\\%s", appDir, GAME_FILE);

    if (!WriteGame(gamePath)) {
        MessageBoxW(NULL, L"Could not unpack the game files.",
                    L"Pocket Arcade", MB_ICONERROR | MB_OK);
        return 1;
    }

    wchar_t url[MAX_PATH + 16];
    ToFileUrl(gamePath, url, MAX_PATH + 16);

    wchar_t programFiles[MAX_PATH] = L"";
    wchar_t programFilesX86[MAX_PATH] = L"";
    GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH);

    const wchar_t *roots[3] = { programFilesX86, programFiles, base };
    const wchar_t *suffixes[2] = {
        L"\\Microsoft\\Edge\\Application\\msedge.exe",
        L"\\Google\\Chrome\\Application\\chrome.exe"
    };

    for (int s = 0; s < 2; s++) {
        for (int r = 0; r < 3; r++) {
            if (roots[r][0] == L'\0') continue;

            wchar_t candidate[MAX_PATH];
            FormatW(candidate, MAX_PATH, L"%s%s", roots[r], suffixes[s]);
            if (TryLaunch(candidate, url, profile)) return 0;
        }
    }

    /* No Chromium browser found: hand the file to whatever is registered. */
    ShellExecuteW(NULL, L"open", gamePath, NULL, NULL, SW_SHOWNORMAL);
    return 0;
}
