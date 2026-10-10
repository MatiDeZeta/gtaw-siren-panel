#include "core/layout.h"

#include "core/logger.h"
#include "core/util.h"

#include <windows.h>
#include <cstdlib>
#include <string>
#include <vector>

namespace layout {

namespace {

const wchar_t* kLeaf = L"gtaw-siren-panel.layout.txt";
const size_t   kMaxBytes = 4096;

const char* kBlocks[] = { "panel" };

bool isBlock(const std::string& s)
{
    for (const char* b : kBlocks)
        if (s == b) return true;
    return false;
}

// A plain decimal number, no exponent, no sign games: what the script writes with toFixed.
bool isNumber(const std::string& s, double lo, double hi)
{
    if (s.empty() || s.size() > 12) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') digits = true;
        else if (c == '.' && !dot) dot = true;
        else return false;
    }
    if (!digits) return false;
    double v = std::strtod(s.c_str(), nullptr);
    return v >= lo && v <= hi;
}

}  // namespace

bool isValid(const std::string& text)
{
    if (text.size() > kMaxBytes) return false;
    for (const std::string& raw : util::split(text, '\n')) {
        std::string line = util::trim(raw);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return false;
        if (!isBlock(util::trim(line.substr(0, eq)))) return false;

        std::vector<std::string> v = util::split(util::trim(line.substr(eq + 1)), ' ');
        std::vector<std::string> f;
        for (const std::string& t : v) if (!t.empty()) f.push_back(t);
        if (f.size() != 4) return false;
        if (!isNumber(f[0], -1.0, 1.0) || !isNumber(f[1], -1.0, 1.0) ||
            !isNumber(f[2], 0.25, 4.0) || (f[3] != "0" && f[3] != "1"))
            return false;
    }
    return true;
}

std::string load()
{
    std::string text;
    if (!util::readBinaryFile(util::modulePath(kLeaf), kMaxBytes, text)) return std::string();
    std::string bare;
    bare.reserve(text.size());
    for (char c : text) if (c != '\r') bare.push_back(c);
    text.swap(bare);
    if (!isValid(text)) {
        LOG_WARN("%ls is not a layout this plugin wrote; ignoring it", kLeaf);
        return std::string();
    }
    return text;
}

bool save(const std::string& text)
{
    if (!isValid(text)) {
        LOG_WARN("The page handed back a layout that does not validate; not written");
        return false;
    }
    std::wstring path = util::modulePath(kLeaf);
    if (path.empty()) return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN("Could not write %ls (err %lu)", kLeaf, GetLastError());
        return false;
    }
    std::string out;
    for (const std::string& raw : util::split(text, '\n')) {
        std::string line = util::trim(raw);
        if (!line.empty()) out += line + "\r\n";
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, out.data(), DWORD(out.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok) { LOG_WARN("Could not write %ls (err %lu)", kLeaf, GetLastError()); return false; }
    LOG_INFO("Layout saved to %ls (%zu bytes)", kLeaf, out.size());
    return true;
}

}  // namespace layout
