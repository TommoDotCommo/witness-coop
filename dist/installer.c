/* Native entry point of the Witness co-op setup. The installer window and logic are readable PowerShell (installer-gui.ps1, witness-coop.ps1,
 * install-state.ps1); this program only gets them to disk safely and starts them. Two builds of this one file:
 *
 *  - ONE-FILE setup (HAVE_PAYLOAD): the scripts, the proxy, the launcher and the model are inside this exe as one container (resource 100).
 *    Its SHA-256 is compiled in and checked first. The files are written to a fresh folder %LOCALAPPDATA%\witness-coop\setup\<version>\
 *    (created exclusively; a folder that already exists is never reused, and a link anywhere on the way is refused), PowerShell runs the window
 *    from there, and the folder is removed when the window closes.
 *  - the thin Setup.exe inside the zip (no payload): runs the scripts that sit beside it. If they are not there it says why in plain words
 *    (most often: it was started from inside the zip) and where to get the one-file setup.
 *
 * Test hooks (environment): WITNESS_SETUP_MESSAGE_FILE writes the message to that file instead of showing a box;
 * --extract-only unpacks and stops (no window, no cleanup). Mutants for the tests: SETUP_MUT_NO_HASH_CHECK, SETUP_MUT_FOLLOW_LINKS, SETUP_MUT_REUSE_DIR. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include "setup-core.h"

#ifndef SETUP_VERSION
#define SETUP_VERSION "dev"
#endif
enum { EXIT_OK = 0, EXIT_DAMAGED = 2, EXIT_PACKAGE = 3, EXIT_LINK = 4, EXIT_WRITE = 5, EXIT_FILES = 6, EXIT_ZIP = 7, EXIT_POWERSHELL = 8, EXIT_NOHOME = 9 };

static void widen(const char* s, wchar_t* out, int n) { MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n); out[n - 1] = 0; }

static void say(const char* text, int icon)
{
    wchar_t file[MAX_PATH];
    static wchar_t wide[4096];
    widen(text, wide, 4096);
    if (GetEnvironmentVariableW(L"WITNESS_SETUP_MESSAGE_FILE", file, MAX_PATH) && file[0]) {
        HANDLE h = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, text, (DWORD)strlen(text), &w, NULL); WriteFile(h, "\r\n", 2, &w, NULL); CloseHandle(h); }
        return;
    }
    MessageBoxW(NULL, wide, L"The Witness Co-op Setup", MB_OK | icon);
}

static int fail(int code, const char* text) { say(text, MB_ICONERROR); return code; }

static inline int is_link(const wchar_t* p)
{
#ifdef SETUP_MUT_FOLLOW_LINKS
    (void)p; return 0;
#else
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#endif
}

#ifdef HAVE_PAYLOAD
/* Make sure p is a real (not linked) directory, creating it when it is missing. A directory that was already there is fine. */
static int ensure_dir(const wchar_t* p)
{
    DWORD a;
    if (CreateDirectoryW(p, NULL)) return 1;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !is_link(p);
}

static inline int under(const wchar_t* path, const wchar_t* dir)      /* path is inside dir (both final paths) */
{
    size_t n = wcslen(dir);
    return _wcsnicmp(path, dir, n) == 0 && path[n] == L'\\';
}

/* Unpack the container into a fresh folder below base. dirOut gets the folder. */
static int extract(const uint8_t* buf, size_t len, wp_entry* e, int count, const wchar_t* base, wchar_t* dirOut, unsigned char* created)
{
    wchar_t root[MAX_PATH], wver[80], dirFinal[MAX_PATH * 2], tmp[MAX_PATH * 2];
    HANDLE dirHandle;
    int i, tries;
    (void)buf; (void)len;
    widen(SETUP_VERSION, wver, 80);
    if (wcslen(base) + 40 + wcslen(wver) >= MAX_PATH - 100) return EXIT_NOHOME;
    swprintf(root, MAX_PATH, L"%ls\\witness-coop", base);
    if (!ensure_dir(root)) return EXIT_LINK;
    swprintf(root, MAX_PATH, L"%ls\\witness-coop\\setup", base);
    if (!ensure_dir(root)) return EXIT_LINK;
    for (tries = 1; tries < 100; tries++) {
        if (tries == 1) swprintf(dirOut, MAX_PATH, L"%ls\\%ls", root, wver); else swprintf(dirOut, MAX_PATH, L"%ls\\%ls-%d", root, wver, tries);
        if (CreateDirectoryW(dirOut, NULL)) break;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return EXIT_WRITE;
#ifdef SETUP_MUT_REUSE_DIR
        break;                                                  /* mutant: whatever is there is used, a planted link included */
#endif
    }
    if (tries >= 100) return EXIT_WRITE;
    dirHandle = CreateFileW(dirOut, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (dirHandle == INVALID_HANDLE_VALUE) return EXIT_WRITE;
    dirFinal[0] = 0;
    if (!GetFinalPathNameByHandleW(dirHandle, dirFinal, MAX_PATH * 2, FILE_NAME_NORMALIZED)) { CloseHandle(dirHandle); return EXIT_WRITE; }
    if (is_link(dirOut)) { CloseHandle(dirHandle); return EXIT_LINK; }
    for (i = 0; i < count; i++) {
        wchar_t rel[WP_MAX_NAME + 1], file[MAX_PATH * 2], final[MAX_PATH * 2];
        HANDLE h; DWORD done; size_t at = 0, k;
        for (k = 0; k < e[i].nameLen; k++) rel[k] = e[i].name[k] == '/' ? L'\\' : (wchar_t)e[i].name[k];
        rel[e[i].nameLen] = 0;
        swprintf(file, MAX_PATH * 2, L"%ls\\%ls", dirOut, rel);
        for (k = 0; rel[k]; k++) if (rel[k] == L'\\') {         /* sub folders on the way, each created here */
            wchar_t sub[MAX_PATH * 2]; swprintf(sub, MAX_PATH * 2, L"%ls\\%.*ls", dirOut, (int)k, rel);
            if (!CreateDirectoryW(sub, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) { CloseHandle(dirHandle); return EXIT_WRITE; }
            if (is_link(sub)) { CloseHandle(dirHandle); return EXIT_LINK; }
        }
        h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (h == INVALID_HANDLE_VALUE) { CloseHandle(dirHandle); return EXIT_WRITE; }
        created[i] = 1;
#ifndef SETUP_MUT_FOLLOW_LINKS
        final[0] = 0;       /* the file really is below the folder we made, whatever links were swapped in since */
        if (!GetFinalPathNameByHandleW(h, final, MAX_PATH * 2, FILE_NAME_NORMALIZED) || !under(final, dirFinal)) { CloseHandle(h); CloseHandle(dirHandle); return EXIT_LINK; }
#else
        (void)final; (void)dirFinal; (void)under;
#endif
        while (at < e[i].size) {
            DWORD chunk = e[i].size - at > (1u << 20) ? (1u << 20) : (DWORD)(e[i].size - at);
            if (!WriteFile(h, e[i].data + at, chunk, &done, NULL) || done != chunk) { CloseHandle(h); CloseHandle(dirHandle); return EXIT_WRITE; }
            at += done;
        }
        if (!CloseHandle(h)) { CloseHandle(dirHandle); return EXIT_WRITE; }
    }
    (void)tmp;
    CloseHandle(dirHandle);
    return EXIT_OK;
}

/* Remove what extract() wrote: our files, then the folders (RemoveDirectory only takes empty ones, so nothing foreign is lost). */
static void cleanup(wp_entry* e, int count, const wchar_t* dir, const unsigned char* created)
{
    int i, depth;
    for (i = 0; i < count; i++) if (created[i]) {
        wchar_t rel[WP_MAX_NAME + 1], file[MAX_PATH * 2]; size_t k;
        for (k = 0; k < e[i].nameLen; k++) rel[k] = e[i].name[k] == '/' ? L'\\' : (wchar_t)e[i].name[k];
        rel[e[i].nameLen] = 0; swprintf(file, MAX_PATH * 2, L"%ls\\%ls", dir, rel); DeleteFileW(file);
    }
    for (depth = 4; depth >= 1; depth--) for (i = 0; i < count; i++) {
        wchar_t rel[WP_MAX_NAME + 1], sub[MAX_PATH * 2]; size_t k; int seen = 0;
        for (k = 0; k < e[i].nameLen; k++) rel[k] = e[i].name[k] == '/' ? L'\\' : (wchar_t)e[i].name[k];
        rel[e[i].nameLen] = 0;
        for (k = 0; rel[k]; k++) if (rel[k] == L'\\' && ++seen == depth) { swprintf(sub, MAX_PATH * 2, L"%ls\\%.*ls", dir, (int)k, rel); RemoveDirectoryW(sub); break; }
    }
    RemoveDirectoryW(dir);
}

#endif

static int run_powershell(const wchar_t* script, int wait)
{
    wchar_t powershell[MAX_PATH], commandLine[MAX_PATH * 4];
    STARTUPINFOW startup = { 0 };
    PROCESS_INFORMATION process = { 0 };
    DWORD code = 0;
    if (!GetSystemDirectoryW(powershell, MAX_PATH) || wcslen(powershell) + wcslen(L"\\WindowsPowerShell\\v1.0\\powershell.exe") >= MAX_PATH) return EXIT_POWERSHELL;
    wcscat(powershell, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    /* Bypass is for this one process and this one script only: the script is either unpacked from this signed, hash-checked exe a moment ago
     * or is the readable file beside the thin Setup. Windows' default policy refuses every unsigned script, which would stop the window itself. */
    swprintf(commandLine, MAX_PATH * 4, L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -File \"%ls\"", powershell, script);
    startup.cb = sizeof(startup);
    if (!CreateProcessW(powershell, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        say("Could not start the installer window. Windows PowerShell is required.", MB_ICONERROR);
        return EXIT_POWERSHELL;
    }
    if (wait) { WaitForSingleObject(process.hProcess, INFINITE); GetExitCodeProcess(process.hProcess, &code); }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)code;
}

#ifdef HAVE_PAYLOAD
#ifndef PAYLOAD_SHA256
#error "PAYLOAD_SHA256 (64 hex digits of the container) must be given with HAVE_PAYLOAD"
#endif
static int onefile(int extractOnly)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(100), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const uint8_t* payload = g ? (const uint8_t*)LockResource(g) : NULL;
    size_t len = r ? SizeofResource(NULL, r) : 0;
    static wp_entry entries[WP_MAX_FILES]; static unsigned char created[WP_MAX_FILES];
    wchar_t base[MAX_PATH], dir[MAX_PATH], script[MAX_PATH * 2], note[MAX_PATH * 2 + 40];
    int count = 0, rc;
    if (!payload || !len) return fail(EXIT_DAMAGED, WP_MSG_DAMAGED);
#ifndef SETUP_MUT_NO_HASH_CHECK
    if (!wp_hash_ok(payload, len, PAYLOAD_SHA256)) return fail(EXIT_DAMAGED, WP_MSG_DAMAGED);
#endif
    if (wp_parse(payload, len, entries, WP_MAX_FILES, &count) != WP_OK) return fail(EXIT_PACKAGE, WP_MSG_DAMAGED);
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) || !base[0]) return fail(EXIT_NOHOME, "Setup needs your user profile folder (%LOCALAPPDATA%) to unpack into, and Windows does not report one.");
    memset(created, 0, sizeof created);
    rc = extract(payload, len, entries, count, base, dir, created);
    if (rc == EXIT_LINK) { cleanup(entries, count, dir, created); return fail(rc, "Setup stopped: a folder it needs below your user profile (witness-coop\\setup) is a link or was swapped for one while Setup was unpacking. Nothing was installed. Remove or rename the folder %LOCALAPPDATA%\\witness-coop and run Setup again."); }
    if (rc != EXIT_OK) { cleanup(entries, count, dir, created); return fail(rc, "Setup could not unpack its files into %LOCALAPPDATA%\\witness-coop\\setup (disk full, or a security program blocked the write?). Nothing was installed."); }
    if (extractOnly) { swprintf(note, MAX_PATH * 2 + 40, L"extracted: %ls", dir); { char n8[MAX_PATH * 4]; WideCharToMultiByte(CP_UTF8, 0, note, -1, n8, sizeof n8, NULL, NULL); say(n8, MB_ICONINFORMATION); } return EXIT_OK; }
    swprintf(script, MAX_PATH * 2, L"%ls\\installer-gui.ps1", dir);
    rc = run_powershell(script, 1);
    cleanup(entries, count, dir, created);
    return rc;
}
#endif

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    int argc = 0, i, extractOnly = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    (void)instance; (void)previous; (void)command; (void)show;
    for (i = 1; argv && i < argc; i++) if (!wcscmp(argv[i], L"--extract-only")) extractOnly = 1;
#ifdef HAVE_PAYLOAD
    return onefile(extractOnly);
#else
    {
        wchar_t path[MAX_PATH], script[MAX_PATH], temp[MAX_PATH], *end; char path8[MAX_PATH * 4];
        (void)extractOnly;
        if (!GetModuleFileNameW(NULL, path, MAX_PATH) || !(end = wcsrchr(path, L'\\'))) return EXIT_FILES;
        if ((size_t)(end - path) + 1 + wcslen(L"installer-gui.ps1") >= MAX_PATH) return EXIT_FILES;
        wcscpy(script, path); wcscpy(script + (end - path) + 1, L"installer-gui.ps1");
        if (GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
            int inTemp = GetTempPathW(MAX_PATH, temp) && _wcsnicmp(path, temp, wcslen(temp)) == 0;
            WideCharToMultiByte(CP_UTF8, 0, path, -1, path8, sizeof path8, NULL, NULL);
#ifdef SETUP_MUT_NO_ZIP_MESSAGE
            (void)inTemp; (void)path8;
#else
            if (wp_from_zip(path8) || inTemp) return fail(EXIT_ZIP, WP_MSG_FROM_ZIP);
#endif
            return fail(EXIT_FILES, WP_MSG_MISSING);
        }
        return run_powershell(script, 1) == EXIT_POWERSHELL ? EXIT_POWERSHELL : EXIT_OK;
    }
#endif
}
