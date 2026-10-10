#include "hud/assets.h"

#include "core/util.h"

#include <windows.h>

namespace hud {

bool loadAsset(const wchar_t* name, std::string& bytes)
{
    bytes.clear();
    HRSRC info = FindResourceW(g_self, name, MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!info) return false;
    DWORD size = SizeofResource(g_self, info);
    HGLOBAL handle = LoadResource(g_self, info);
    if (!handle || !size) return false;
    const void* data = LockResource(handle);
    if (!data) return false;
    bytes.assign(static_cast<const char*>(data), size);
    return true;
}

}  // namespace hud
