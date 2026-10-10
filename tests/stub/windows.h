// Just enough Win32 to typecheck the sources on Linux. Signatures follow the documented ones so
// a mismatch here is a mismatch on MSVC too. ASCII-only conversions, which is all the tests need.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

typedef void*          HINSTANCE;
typedef void*          HANDLE;
typedef void*          LPVOID;
typedef void*          PVOID;
typedef unsigned long  DWORD;
typedef unsigned long* LPDWORD;
typedef unsigned long  ULONG;
typedef unsigned short USHORT;
typedef unsigned short WORD;
typedef int            BOOL;
typedef const wchar_t* LPCWSTR;
typedef wchar_t*       LPWSTR;
typedef std::size_t    SIZE_T;
typedef std::size_t    DWORD_PTR;
typedef void*          LPSECURITY_ATTRIBUTES;
typedef void*          HMODULE;
typedef void*          HRSRC;
typedef void*          HGLOBAL;

#define WINAPI
#define MAX_PATH 260
#define CP_UTF8  65001
#define TRUE  1
#define FALSE 0
#define NO_ERROR 0UL
#define INVALID_HANDLE_VALUE   ((HANDLE)(long long)-1)
#define MAKEINTRESOURCEW(i)    ((LPWSTR)(std::size_t)(WORD)(i))
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define GENERIC_READ  0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ  0x1UL
#define FILE_SHARE_WRITE 0x2UL
#define OPEN_EXISTING 3UL
#define CREATE_NEW    1UL
#define CREATE_ALWAYS 2UL
#define FILE_ATTRIBUTE_NORMAL 0x80UL
#define WAIT_TIMEOUT 258UL
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define GetFileExInfoStandard 0

typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

struct CRITICAL_SECTION { void* opaque[8]; };

struct SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

union LARGE_INTEGER { struct { DWORD LowPart; long HighPart; }; long long QuadPart; };

struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };

struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
};

inline int MultiByteToWideChar(unsigned, DWORD, const char* in, int inLen,
                               wchar_t* out, int outLen)
{
    if (inLen < 0) inLen = (int)std::strlen(in);
    if (!out || outLen == 0) return inLen;
    if (outLen < inLen) return 0;
    for (int i = 0; i < inLen; ++i) out[i] = (wchar_t)(unsigned char)in[i];
    return inLen;
}

inline int WideCharToMultiByte(unsigned, DWORD, const wchar_t* in, int inLen,
                               char* out, int outLen, const char*, const char*)
{
    if (inLen < 0) { inLen = 0; while (in[inLen]) ++inLen; }
    if (!out || outLen == 0) return inLen;
    if (outLen < inLen) return 0;
    for (int i = 0; i < inLen; ++i) out[i] = (char)in[i];
    return inLen;
}

DWORD  GetModuleFileNameW(HINSTANCE, LPWSTR, DWORD);
HRSRC  FindResourceW(HMODULE, LPCWSTR, LPCWSTR);
DWORD  SizeofResource(HMODULE, HRSRC);
HGLOBAL LoadResource(HMODULE, HRSRC);
LPVOID LockResource(HGLOBAL);
DWORD  GetLastError();
BOOL   DisableThreadLibraryCalls(HINSTANCE);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
BOOL   SetEvent(HANDLE);
DWORD  WaitForSingleObject(HANDLE, DWORD);
BOOL   CloseHandle(HANDLE);
void   Sleep(DWORD);
void   GetLocalTime(SYSTEMTIME*);
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL   ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
BOOL   WriteFile(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
BOOL   GetFileSizeEx(HANDLE, LARGE_INTEGER*);
DWORD  GetFileAttributesW(LPCWSTR);
BOOL   GetFileAttributesExW(LPCWSTR, int, LPVOID);
BOOL   DeleteFileW(LPCWSTR);
BOOL   MoveFileW(LPCWSTR, LPCWSTR);
void   InitializeCriticalSection(CRITICAL_SECTION*);
void   EnterCriticalSection(CRITICAL_SECTION*);
void   LeaveCriticalSection(CRITICAL_SECTION*);
