// PlayRDR2 wrapper: fixes Epic Preview (Linux) launch args, then starts the real launcher exe.
// Install: rename original PlayRDR2.exe -> PlayRDR2Original.exe, put this file as PlayRDR2.exe.
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

static FILE *logf = NULL;

static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

// "/home/x/y.json" -> "Z:\home\x\y.json"
static void unix_to_win(const wchar_t *in, wchar_t *out, size_t cap) {
    if (in[0] == L'/') { wcscpy(out, L"Z:"); wcsncat(out, in, cap - 4); }
    else wcsncpy(out, in, cap - 1);
    for (wchar_t *p = out; *p; p++) if (*p == L'/') *p = L'\\';
}

static void log_masked(const wchar_t *cmd) {
    if (!logf) return;
    wchar_t *copy = _wcsdup(cmd);
    wchar_t *k = wcsstr(copy, L"-AUTH_PASSWORD=");
    if (k) { for (wchar_t *p = k + 15; *p && *p != L' '; p++) *p = L'*'; }
    fwprintf(logf, L"%ls\n", copy);
    free(copy);
}

static const wchar_t *RGL_NAMES[] = { L"Launcher.exe", L"LauncherPatcher.exe", L"SocialClubHelper.exe",
    L"RockstarService.exe", L"RockstarErrorHandler.exe", NULL };

// count running processes with the given name; if kill != 0, terminate them
static int find_proc(const wchar_t *name, int kill) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, name) == 0) {
            n++;
            if (kill) { HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (h) { TerminateProcess(h, 0); CloseHandle(h); } }
        }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return n;
}
static int any_rgl(int kill) { int n = 0; for (int i = 0; RGL_NAMES[i]; i++) n += find_proc(RGL_NAMES[i], kill); return n; }

int main(void) {
    wchar_t self[MAX_PATH], dir[MAX_PATH], orig[MAX_PATH], tmp[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy(dir, self);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;

    swprintf(tmp, MAX_PATH, L"%ls\\wrapper_log.txt", dir);
    logf = _wfopen(tmp, L"w");

    swprintf(orig, MAX_PATH, L"%ls\\PlayRDR2Original.exe", dir);
    if (!exists(orig)) {
        if (logf) fwprintf(logf, L"ERROR: %ls not found. Rename the original PlayRDR2.exe to PlayRDR2Original.exe\n", orig);
        MessageBoxW(NULL, L"PlayRDR2Original.exe not found.\nRename the original PlayRDR2.exe to PlayRDR2Original.exe.", L"PlayRDR2 wrapper", MB_ICONERROR);
        return 1;
    }

    int fix_path = !(swprintf(tmp, MAX_PATH, L"%ls\\wrapper_nopath.txt", dir), exists(tmp));
    int fix_env  = !(swprintf(tmp, MAX_PATH, L"%ls\\wrapper_noeos.txt", dir), exists(tmp));

    if (fix_env) SetEnvironmentVariableW(L"EOS_LAUNCHED_BY_EPIC", L"1");

    // Rebuild command line from the raw one (keeps Epic's quoting), skipping argv[0].
    const wchar_t *raw = GetCommandLineW();
    const wchar_t *args = raw;
    if (*args == L'"') { args++; while (*args && *args != L'"') args++; if (*args) args++; }
    else { while (*args && *args != L' ') args++; }
    while (*args == L' ') args++;

    size_t cap = wcslen(args) * 2 + MAX_PATH + 64;
    wchar_t *newargs = (wchar_t *)calloc(cap, sizeof(wchar_t));
    wcscpy(newargs, args);

    if (fix_path) {
        // find -eglv2apipath=<value> where value may be quoted/escaped (\"/home/..json\")
        wchar_t *k = wcsstr(newargs, L"-eglv2apipath=");
        if (k) {
            wchar_t *v = k + wcslen(L"-eglv2apipath=");
            wchar_t *start = v;
            while (*start == L'\\' || *start == L'"') start++;
            if (*start == L'/') {
                wchar_t *end = start;
                while (*end && *end != L'"' && !(*end == L'\\' && end[1] == L'"') && *end != L' ') end++;
                wchar_t saved[MAX_PATH * 2] = {0};
                size_t n = (size_t)(end - start); if (n > MAX_PATH - 1) n = MAX_PATH - 1;
                wcsncpy(saved, start, n);
                wchar_t winp[MAX_PATH * 2] = {0};
                unix_to_win(saved, winp, MAX_PATH * 2);
                // Pass as a plain quoted value: -eglv2apipath="Z:\...\api.json"
                wchar_t tail[4096]; wcscpy(tail, end);
                wchar_t *t = tail;
                if (*t == L'\\' && t[1] == L'"') t += 2; else if (*t == L'"') t++;
                *k = 0;
                wcscat(newargs, L"-eglv2apipath=\"");
                wcscat(newargs, winp);
                wcscat(newargs, L"\"");
                wcscat(newargs, t);
            }
        }
    }

    wchar_t *cmd = (wchar_t *)calloc(cap + MAX_PATH + 8, sizeof(wchar_t));
    wchar_t helper[MAX_PATH];
    swprintf(helper, MAX_PATH, L"%ls\\EpicGamesLauncher.exe", dir);
    int use_helper = exists(helper);
    if (use_helper) swprintf(cmd, cap + MAX_PATH, L"\"%ls\" \"%ls\" %ls", helper, orig, newargs);
    else swprintf(cmd, cap + MAX_PATH, L"\"%ls\" %ls", orig, newargs);

    if (logf) {
        fwprintf(logf, L"Wrapper: fix_path=%d fix_eos_env=%d\nIncoming: ", fix_path, fix_env);
        log_masked(raw);
        fwprintf(logf, L"Starting: ");
        log_masked(cmd);
        fflush(logf);
    }

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(use_helper ? helper : orig, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        if (logf) { fwprintf(logf, L"CreateProcess failed: %lu\n", GetLastError()); fclose(logf); }
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    if (logf) { fwprintf(logf, L"Original exited with code %lu\n", code); fflush(logf); }

    // Wait for RDR2.exe to start and finish, then close the Rockstar Launcher so Epic stops showing "Running".
    swprintf(tmp, MAX_PATH, L"%ls\\wrapper_noclose.txt", dir);
    if (!exists(tmp)) {
        int seen = 0; DWORD t0 = GetTickCount();
        for (;;) {
            Sleep(1000);
            int game = find_proc(L"RDR2.exe", 0);
            if (game) { if (!seen && logf) { fwprintf(logf, L"RDR2.exe detected\n"); fflush(logf); } seen = 1; continue; }
            if (seen) { if (logf) fwprintf(logf, L"RDR2.exe exited, closing Rockstar Launcher\n"); break; }
            if (!any_rgl(0) && GetTickCount() - t0 > 30000) { if (logf) fwprintf(logf, L"No game and no launcher, exiting\n"); break; }
        }
        any_rgl(1);
    }
    if (logf) fclose(logf);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
