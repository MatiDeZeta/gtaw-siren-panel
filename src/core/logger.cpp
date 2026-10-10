#include "core/logger.h"

#include "core/util.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace logging {

namespace {

CRITICAL_SECTION g_lock;
bool             g_ready = false;
FILE*            g_file  = nullptr;

// A session can run for many hours and the heartbeat writes on every reconnect, so the file is
// restarted once it passes this. Keeping one previous file means a crash that happened before
// the current launch is still readable.
const long kMaxLogBytes = 512 * 1024;

void rollIfLarge(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return;
    if (info.nFileSizeHigh == 0 && info.nFileSizeLow < kMaxLogBytes) return;

    std::wstring prev = path + L".prev";
    DeleteFileW(prev.c_str());
    MoveFileW(path.c_str(), prev.c_str());
}

}  // namespace

void init()
{
    if (g_ready) return;

    InitializeCriticalSection(&g_lock);
    g_ready = true;

    std::wstring path = util::modulePath(L"gtaw-siren-panel.log");
    if (path.empty()) return;

    rollIfLarge(path);

    // The log uses narrow fprintf, so keep the stream byte oriented. ccs=UTF-8 would make
    // it a Unicode stream whose output functions expect UTF-16 wchar_t data. That mode
    // can leave this log empty and may trigger the CRT's invalid-parameter handler.
    // Non-ASCII text passed to this logger is already UTF-8 and is written as bytes.
    if (_wfopen_s(&g_file, path.c_str(), L"a") != 0) g_file = nullptr;

    write("INFO", "----- gtaw-siren-panel starting -----");
}

void write(const char* level, const char* fmt, ...)
{
    if (!g_ready) return;

    char body[1024];
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
    va_end(args);
    if (n < 0) body[sizeof(body) - 1] = '\0';

    SYSTEMTIME t;
    GetLocalTime(&t);

    EnterCriticalSection(&g_lock);
    if (g_file) {
        fprintf(g_file, "[%04u-%02u-%02u %02u:%02u:%02u] %-5s %s\n",
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, level, body);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_lock);
}

void shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    if (g_file) { fclose(g_file); g_file = nullptr; }
    LeaveCriticalSection(&g_lock);
}

}  // namespace logging
