#include "core/util.h"

#include <cstdint>
#include <cstdlib>
#include <cwchar>

HINSTANCE g_self = nullptr;

namespace util {

std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string lower(const std::string& s)
{
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return o;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(trim(cur));
    return out;
}

bool parseBool(const std::string& raw, bool def)
{
    std::string v = lower(trim(raw));
    if (v == "1" || v == "yes" || v == "true"  || v == "on")  return true;
    if (v == "0" || v == "no"  || v == "false" || v == "off") return false;
    return def;
}

int parseInt(const std::string& raw, int def, int lo, int hi)
{
    std::string v = trim(raw);
    if (v.empty()) return def;

    char* end = nullptr;
    long parsed = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || (end && *end != '\0')) return def;
    if (parsed < lo || parsed > hi) return def;
    return int(parsed);
}

double parseDouble(const std::string& raw, double def, double lo, double hi)
{
    std::string v = trim(raw);
    if (v.empty()) return def;

    char* end = nullptr;
    double parsed = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || (end && *end != '\0')) return def;
    // Rejects NaN and infinity too: neither comparison holds for NaN.
    if (!(parsed >= lo && parsed <= hi)) return def;
    return parsed;
}

bool isHexColor(const std::string& s)
{
    if (s.size() != 4 && s.size() != 7) return false;
    if (s[0] != '#') return false;
    for (size_t i = 1; i < s.size(); ++i) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

std::wstring widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &out[0], n);
    return out;
}

std::string narrow(const std::wstring& s)
{
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring moduleDir()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();

    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return std::wstring();
    return path.substr(0, slash + 1);
}

std::wstring modulePath(const wchar_t* leaf)
{
    std::wstring dir = moduleDir();
    if (dir.empty()) return std::wstring();
    return dir + leaf;
}


bool readBinaryFile(const std::wstring& path, size_t maxBytes, std::string& out)
{
    out.clear();
    if (path.empty()) return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        uint64_t(size.QuadPart) > uint64_t(maxBytes)) {
        CloseHandle(h);
        return false;
    }

    std::string buf(size_t(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, &buf[0], DWORD(buf.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != buf.size()) return false;

    out.swap(buf);
    return true;
}

std::string base64(const std::string& bytes)
{
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        unsigned v = (unsigned char)bytes[i] << 16 |
                     (unsigned char)bytes[i + 1] << 8 |
                     (unsigned char)bytes[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }

    size_t left = bytes.size() - i;
    if (left == 1) {
        unsigned v = (unsigned)(unsigned char)bytes[i] << 16;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out += "==";
    } else if (left == 2) {
        unsigned v = (unsigned)(unsigned char)bytes[i] << 16 |
                     (unsigned)(unsigned char)bytes[i + 1] << 8;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

}  // namespace util
