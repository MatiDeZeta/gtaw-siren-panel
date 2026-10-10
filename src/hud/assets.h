#pragma once

#include <string>

// The panel images and the fallback font are ordinary RCDATA resources in the .asi, the way any
// DLL carries its pictures, rather than base64 text in the code. They are read out of the module
// and encoded for the page only when the HUD is installed.
namespace hud {

// Copies the RCDATA resource of that name out of this module. False when it is not there.
bool loadAsset(const wchar_t* name, std::string& bytes);

}  // namespace hud
