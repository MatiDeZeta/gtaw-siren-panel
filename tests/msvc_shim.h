// Enough of the MSVC CRT to typecheck the sources on this Linux box.
#pragma once
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#define _TRUNCATE 0
#define _snprintf_s(buf, size, count, ...) snprintf(buf, size, __VA_ARGS__)
#define _vsnprintf_s(buf, size, count, fmt, args) vsnprintf(buf, size, fmt, args)
int _wfopen_s(FILE** f, const wchar_t* path, const wchar_t* mode);
