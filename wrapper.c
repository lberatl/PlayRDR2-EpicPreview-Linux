// PlayRDR2 wrapper for the Epic Games Launcher Preview (Linux / Proton)
//
//  - converts Epic's Linux -eglv2apipath to a Windows (Z:\) path
//  - starts the real launcher exe through EpicGamesLauncher.exe (Rockstar wants that parent process name)
//  - when RDR2.exe exits, closes the Rockstar Launcher so Epic stops showing "Running"
//
// Install: rename the original PlayRDR2.exe -> PlayRDR2Original.exe and put this file as PlayRDR2.exe.
// Optional marker files in the game folder: wrapper_nopath.txt, wrapper_noeos.txt, wrapper_noclose.txt
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

// ---- BEGIN PURE (no Windows API, unit-tested separately) ----
static wchar_t *dupw(const wchar_t *s) {
    size_t n = wcslen(s) + 1;
    wchar_t *d = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (d) memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

// Skip argv[0] of a raw command line.
static const wchar_t *skip_argv0(const wchar_t *a) {
    if (*a == L'"') { a++; while (*a && *a != L'"') a++; if (*a) a++; }
    else { while (*a && *a != L' ') a++; }
    while (*a == L' ') a++;
    return a;
}

// Epic passes -eglv2apipath=\"/linux/path/api.json\". Rewrite it to -eglv2apipath="Z:\linux\path\api.json".
// Returns a new string (caller frees). Returns an unchanged copy if the argument is missing or not a Linux path.
static wchar_t *fix_api_path(const wchar_t *args, int *changed) {
    static const wchar_t key[] = L"-eglv2apipath=";
    *changed = 0;
    const wchar_t *k = wcsstr(args, key);
    if (!k) return dupw(args);
    const wchar_t *s = k + wcslen(key);
    int quoted = 0;
    while (*s == L'\\' || *s == L'"') { quoted = 1; s++; }
    if (*s != L'/') return dupw(args);
    const wchar_t *e = s;
    while (*e) {
        if (quoted && (*e == L'"' || (*e == L'\\' && e[1] == L'"'))) break;
        if (!quoted && *e == L' ') break;
        e++;
    }
    const wchar_t *tail = e;
    if (*tail == L'\\' && tail[1] == L'"') tail += 2; else if (*tail == L'"') tail++;
    size_t plen = (size_t)(e - s), pre = (size_t)(k - args);
    wchar_t *out = (wchar_t *)calloc(wcslen(args) + plen + 16, sizeof(wchar_t));
    if (!out) return dupw(args);
    memcpy(out, args, pre * sizeof(wchar_t));
    wcscat(out, key);
    wcscat(out, L"\"Z:");
    size_t pos = wcslen(out);
    for (size_t i = 0; i < plen; i++) out[pos + i] = (s[i] == L'/') ? L'\\' : s[i];
    wcscat(out, L"\"");
    wcscat(out, tail);
    *changed = 1;
    return out;
}

// Replace the value after `key` with '*' (up to the closing quote if the argument is quoted, else up to a space).
static void mask_value(wchar_t *s, const wchar_t *key) {
    wchar_t *k = s;
    while ((k = wcsstr(k, key)) != NULL) {
        wchar_t *p = k + wcslen(key);
        int q = (k > s && k[-1] == L'"');
        while (*p && !(q ? (*p == L'"') : (*p == L' '))) *p++ = L'*';
        k = p;
    }
}

// Hide the user name in ".../home/<name>/..." and "...\home\<name>\..." paths.
static void mask_home(wchar_t *s, const wchar_t *pat, wchar_t sep) {
    wchar_t *k = s;
    while ((k = wcsstr(k, pat)) != NULL) {
        wchar_t *p = k + wcslen(pat);
        while (*p && *p != sep && *p != L'"' && *p != L' ') *p++ = L'*';
        k = p;
    }
}

// Make a string safe to put in a log that people will share.
static void mask_secrets(wchar_t *s) {
    mask_value(s, L"-AUTH_PASSWORD=");
    mask_value(s, L"-epicusername=");
    mask_value(s, L"-epicuserid=");
    mask_home(s, L"\\home\\", L'\\');
    mask_home(s, L"/home/", L'/');
}
// ---- END PURE ----

static FILE *logf = NULL;

static void log_text(const wchar_t *label, const wchar_t *text) {
    if (!logf) return;
    wchar_t *copy = dupw(text);
    if (!copy) return;
    mask_secrets(copy);
    fwprintf(logf, L"%ls%ls\n", label, copy);
    fflush(logf);
    free(copy);
}

static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static int marker(const wchar_t *dir, const wchar_t *name) {
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\%ls", dir, name);
    return exists(p);
}

enum { K_GAME = 1, K_RGL = 2, K_CHAIN = 4 };

static int kind_of(const wchar_t *exe) {
    static const wchar_t *rgl[] = { L"Launcher.exe", L"LauncherPatcher.exe", L"SocialClubHelper.exe",
                                    L"RockstarService.exe", L"RockstarErrorHandler.exe", NULL };
    if (!_wcsicmp(exe, L"RDR2.exe")) return K_GAME;
    for (int i = 0; rgl[i]; i++) if (!_wcsicmp(exe, rgl[i])) return K_RGL;
    if (!_wcsicmp(exe, L"PlayRDR2Original.exe") || !_wcsicmp(exe, L"EpicGamesLauncher.exe")) return K_CHAIN;
    return 0;
}

// One process snapshot: returns which kinds are running; terminates the kinds in kill_mask.
static int scan(int kill_mask) {
    int found = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            int kind = kind_of(pe.szExeFile);
            if (!kind) continue;
            found |= kind;
            if (kind & kill_mask) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

int main(void) {
    wchar_t self[MAX_PATH], dir[MAX_PATH], path[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy(dir, self);
    wchar_t *sl = wcsrchr(dir, L'\\');
    if (sl) *sl = 0;

    swprintf(path, MAX_PATH * 2, L"%ls\\wrapper_log.txt", dir);
    logf = _wfopen(path, L"w, ccs=UTF-8");

    wchar_t orig[MAX_PATH * 2], helper[MAX_PATH * 2];
    swprintf(orig, MAX_PATH * 2, L"%ls\\PlayRDR2Original.exe", dir);
    swprintf(helper, MAX_PATH * 2, L"%ls\\EpicGamesLauncher.exe", dir);

    if (!exists(orig)) {
        log_text(L"ERROR: not found: ", orig);
        MessageBoxW(NULL, L"PlayRDR2Original.exe not found.\nRename the original PlayRDR2.exe to PlayRDR2Original.exe.",
                    L"PlayRDR2 wrapper", MB_ICONERROR);
        if (logf) fclose(logf);
        return 1;
    }

    int fix_path = !marker(dir, L"wrapper_nopath.txt");
    int fix_env = !marker(dir, L"wrapper_noeos.txt");
    int autoclose = !marker(dir, L"wrapper_noclose.txt");
    int use_helper = exists(helper);

    if (fix_env) SetEnvironmentVariableW(L"EOS_LAUNCHED_BY_EPIC", L"1");

    const wchar_t *raw = GetCommandLineW();
    const wchar_t *args = skip_argv0(raw);
    int changed = 0;
    wchar_t *newargs = fix_path ? fix_api_path(args, &changed) : dupw(args);
    if (!newargs) return 1;

    size_t cmdlen = wcslen(helper) + wcslen(orig) + wcslen(newargs) + 16;
    wchar_t *cmd = (wchar_t *)calloc(cmdlen, sizeof(wchar_t));
    if (!cmd) return 1;
    if (use_helper) swprintf(cmd, cmdlen, L"\"%ls\" \"%ls\" %ls", helper, orig, newargs);
    else swprintf(cmd, cmdlen, L"\"%ls\" %ls", orig, newargs);

    if (logf) {
        fwprintf(logf, L"Options: fix_path=%d (changed=%d) fix_eos_env=%d autoclose=%d helper=%d\n",
                 fix_path, changed, fix_env, autoclose, use_helper);
        if (!use_helper)
            fwprintf(logf, L"WARNING: EpicGamesLauncher.exe is missing, Rockstar will probably show BUY NOW\n");
    }
    log_text(L"Incoming: ", raw);
    log_text(L"Starting: ", cmd);

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(use_helper ? helper : orig, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        if (logf) { fwprintf(logf, L"CreateProcess failed: %lu\n", GetLastError()); fclose(logf); }
        return 1;
    }

    if (!autoclose) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    } else {
        // Watch the game from the start. Do not wait for the launcher chain first: it can stay alive
        // as long as the Rockstar Launcher does.
        int seen = 0, chain_done = 0;
        DWORD t0 = GetTickCount();
        for (;;) {
            Sleep(1000);
            if (!chain_done && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                chain_done = 1;
                if (logf) { fwprintf(logf, L"Launcher chain exited\n"); fflush(logf); }
            }
            int found = scan(0);
            if (found & K_GAME) {
                if (!seen && logf) { fwprintf(logf, L"RDR2.exe detected\n"); fflush(logf); }
                seen = 1;
                continue;
            }
            if (seen) {
                if (logf) fwprintf(logf, L"RDR2.exe exited, closing Rockstar Launcher\n");
                break;
            }
            if (!(found & K_RGL) && GetTickCount() - t0 > 30000) {
                if (logf) fwprintf(logf, L"No game and no Rockstar Launcher running, exiting\n");
                break;
            }
        }
        scan(K_RGL | K_CHAIN);
    }

    DWORD code = 0;
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    if (logf) { fwprintf(logf, L"Done (exit code %lu)\n", code); fclose(logf); }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
