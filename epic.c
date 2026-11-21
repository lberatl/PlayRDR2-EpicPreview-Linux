// EpicGamesLauncher.exe: runs its own arguments as a new command and waits.
// Rockstar's launcher requires its parent process to have exactly this name.
#include <windows.h>
#include <wchar.h>
int main(void) {
    const wchar_t *a = GetCommandLineW();
    if (*a == L'"') { a++; while (*a && *a != L'"') a++; if (*a) a++; }
    else { while (*a && *a != L' ') a++; }
    while (*a == L' ') a++;
    if (!*a) return 2;
    wchar_t *cmd = _wcsdup(a);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
