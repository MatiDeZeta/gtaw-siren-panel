// Link-time bodies for the stubbed Win32 calls. File operations are backed by real POSIX files
// so the settings reader and writer can be exercised; threads and locks do nothing.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

// Set by a test to the path the module should pretend to have been loaded from. Backslashes,
// because moduleDir() splits on them; they are translated on the way to fopen.
std::wstring g_stubModulePath;

static std::string narrowPath(LPCWSTR w)
{
    std::string s;
    for (; *w; ++w) s.push_back(*w == L'\\' ? '/' : (char)*w);
    return s;
}

// glibc also supports ccs= UTF streams. Remove the space MSVC accepts after the comma so
// it creates a wide-oriented stream on Linux too; narrow fprintf then reproduces the
// empty log symptom instead of silently writing bytes.
int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode)
{
    std::string m;
    for (const wchar_t* c = mode; *c; ++c)
        if (*c != L' ') m.push_back(char(*c));
    *out = std::fopen(narrowPath(path).c_str(), m.c_str());
    return *out ? 0 : 1;
}

DWORD GetModuleFileNameW(HINSTANCE, LPWSTR buf, DWORD n)
{
    if (g_stubModulePath.empty()) return 0;
    DWORD len = (DWORD)g_stubModulePath.size();
    if (len >= n) return 0;
    std::memcpy(buf, g_stubModulePath.c_str(), (len + 1) * sizeof(wchar_t));
    return len;
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition,
                   DWORD, HANDLE)
{
    std::string p = narrowPath(path);
    if (disposition == CREATE_NEW) {
        if (FILE* probe = std::fopen(p.c_str(), "rb")) { std::fclose(probe); return INVALID_HANDLE_VALUE; }
    }
    FILE* f = std::fopen(p.c_str(), (access & GENERIC_WRITE) ? "wb" : "rb");
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}

BOOL ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPVOID)
{
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    size_t got = std::fread(buf, 1, n, (FILE*)h);
    if (read) *read = (DWORD)got;
    return TRUE;
}

BOOL WriteFile(HANDLE h, const void* buf, DWORD n, LPDWORD written, LPVOID)
{
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    size_t put = std::fwrite(buf, 1, n, (FILE*)h);
    if (written) *written = (DWORD)put;
    return TRUE;
}

BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER* size)
{
    if (!h || h == INVALID_HANDLE_VALUE || !size) return FALSE;
    long cur = std::ftell((FILE*)h);
    std::fseek((FILE*)h, 0, SEEK_END);
    size->QuadPart = std::ftell((FILE*)h);
    std::fseek((FILE*)h, cur, SEEK_SET);
    return TRUE;
}

BOOL CloseHandle(HANDLE h)
{
    if (h && h != INVALID_HANDLE_VALUE) std::fclose((FILE*)h);
    return TRUE;
}

DWORD GetFileAttributesW(LPCWSTR path)
{
    FILE* f = std::fopen(narrowPath(path).c_str(), "rb");
    if (!f) return INVALID_FILE_ATTRIBUTES;
    std::fclose(f);
    return FILE_ATTRIBUTE_NORMAL;
}

DWORD  GetLastError() { return 0; }
BOOL   DisableThreadLibraryCalls(HINSTANCE) { return TRUE; }
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) { return nullptr; }
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return nullptr; }
BOOL   SetEvent(HANDLE) { return TRUE; }
DWORD  WaitForSingleObject(HANDLE, DWORD) { return WAIT_TIMEOUT; }
void   Sleep(DWORD) {}
void   GetLocalTime(SYSTEMTIME* t) { if (t) *t = SYSTEMTIME{}; }
BOOL   GetFileAttributesExW(LPCWSTR, int, LPVOID) { return FALSE; }
BOOL   DeleteFileW(LPCWSTR) { return TRUE; }
BOOL   MoveFileW(LPCWSTR, LPCWSTR) { return TRUE; }
void   InitializeCriticalSection(CRITICAL_SECTION*) {}
void   EnterCriticalSection(CRITICAL_SECTION*) {}
void   LeaveCriticalSection(CRITICAL_SECTION*) {}
