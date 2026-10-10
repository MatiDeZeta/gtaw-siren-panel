// The resource calls, backed by the same files gtaw-siren-panel.rc compiles in, so the generated
// expression is built from the real panel images exactly as the .asi would build it.
// STUB_ASSET_ROOT is the repository root, passed in by run.sh.
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <map>
#include <string>

static std::string g_root = STUB_ASSET_ROOT;

// Loaded once per name and kept for the process, as a mapped resource is.
static std::map<std::string, std::string> g_loaded;

HRSRC FindResourceW(HMODULE, LPCWSTR name, LPCWSTR type)
{
    if ((std::size_t)type != 10 || (std::size_t)name < 0x10000) return nullptr;
    std::string n;
    for (; *name; ++name) n.push_back((char)*name);

    std::string path;
    if (n.compare(0, 3, "KB_") == 0) {
        std::string leaf;
        for (size_t i = 3; i < n.size(); ++i) leaf.push_back((char)std::tolower((unsigned char)n[i]));
        path = g_root + "/assets/kabel/" + leaf + ".png";
    }
    else return nullptr;

    auto hit = g_loaded.find(path);
    if (hit != g_loaded.end()) return &hit->second;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::string* bytes = &g_loaded[path];
    char buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes->append(buf, got);
    std::fclose(f);
    return bytes;
}

DWORD   SizeofResource(HMODULE, HRSRC r) { return r ? (DWORD)static_cast<std::string*>(r)->size() : 0; }
HGLOBAL LoadResource(HMODULE, HRSRC r)   { return r; }
LPVOID  LockResource(HGLOBAL g)          { return g ? (LPVOID)static_cast<std::string*>(g)->data() : nullptr; }
