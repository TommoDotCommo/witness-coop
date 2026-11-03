// A fixture dsound.chain.dll for tools/qa_dropin.sh (a live game): another mod that was named dsound.dll. When it is loaded it writes
// witness_chain_loaded.txt beside itself (the process id), so the test reads the fact from a file the game process made.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(inst, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) slash[1] = 0;
    wcscat_s(path, L"witness_chain_loaded.txt");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { char b[32]; int n = snprintf(b, sizeof b, "%lu\r\n", GetCurrentProcessId()); DWORD w; WriteFile(f, b, n, &w, nullptr); CloseHandle(f); }
    return TRUE;
}
