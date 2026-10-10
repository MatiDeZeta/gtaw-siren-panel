#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Set in DllMain. Used to locate the folder the .asi was loaded from, which is where the
// settings file and the log live.
extern HINSTANCE g_self;

namespace util {

std::string trim(const std::string& s);
std::string lower(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);

// Value parsers used by the settings file. Each one keeps the caller's default when the raw
// text is not something it recognises, so a typo in the settings file degrades to the default
// instead of to zero.
bool   parseBool(const std::string& raw, bool def);
int    parseInt(const std::string& raw, int def, int lo, int hi);
double parseDouble(const std::string& raw, double def, double lo, double hi);

// True for "#rgb" and "#rrggbb" only. Colours from the settings file are pasted into a
// stylesheet, so anything that is not exactly a hex colour is rejected rather than escaped.
bool isHexColor(const std::string& s);

std::wstring widen(const std::string& s);
std::string  narrow(const std::wstring& s);

// Directory holding this module, with a trailing backslash. Empty if it cannot be determined.
std::wstring moduleDir();
std::wstring modulePath(const wchar_t* leaf);

// Reads a file whole. Returns false, and leaves out empty, for a missing file, a read failure,
// or a file larger than maxBytes. Used for the optional font next to the .asi, which is the one
// file the plugin reads that it did not write itself.
bool readBinaryFile(const std::wstring& path, size_t maxBytes, std::string& out);

// Standard base64, no line breaks. The output goes into a data: URI in the stylesheet, so it is
// alphanumerics, + / and = and nothing else, whatever the input bytes were.
std::string base64(const std::string& bytes);

}  // namespace util
