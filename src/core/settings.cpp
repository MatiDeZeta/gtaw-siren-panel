#include "core/settings.h"

#include "core/logger.h"
#include "core/util.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

Settings g_set;

namespace {

const wchar_t* kSettingsLeaf = L"gtaw-siren-panel.settings.txt";

const char* kDefaultFile =
    "# gtaw-siren-panel settings\r\n"
    "#\r\n"
    "# Lines starting with # are ignored. Change the value after the = and restart FiveM.\r\n"
    "# Delete this file to get it back with every option at its default.\r\n"
    "\r\n"
    "# Set to yes to disable the plugin without removing the file.\r\n"
    "off = no\r\n"
    "\r\n"
    "# FiveM's local CEF debugging port. Only change this if you know it moved.\r\n"
    "port = 13172\r\n"
    "\r\n"
    "# ---- when the panel is shown -------------------------------------------------------\r\n"
    "# It is drawn while you are in an emergency vehicle, on top of the speedometer. Move and\r\n"
    "# size it with /hudlayout: while the editor is open the panel can be dragged, scaled with\r\n"
    "# the mouse wheel and hidden with its cross. That is kept in gtaw-siren-panel.layout.txt.\r\n"
    "#\r\n"
    "# hold_ms is how long it stays up after what it shows last changed, in milliseconds: the\r\n"
    "# lights, the siren or its tone, or any key (GUN LOCK when you lock the doors, say). 0 keeps\r\n"
    "# it up the whole time you are in the vehicle. 5000 shows it for five seconds and fades it.\r\n"
    "hold_ms = 0\r\n"
    "\r\n"
    "# ---- what it shows ------------------------------------------------------------------\r\n"
    "# Where the OFF / 1 / 2 / 3 slide knob sits for lights without the siren: 1, 2 or 3. Lights\r\n"
    "# off is always OFF and lights with the siren is always 3. The LEDs above it count the stage.\r\n"
    "lights_stage = 2\r\n"
    "\r\n"
    "# Which key lights up for each siren tone, tone 1 first, comma separated: any of sub, sbs,\r\n"
    "# wail, yelp, wig, bumr or none. A tone past the end of the list lights nothing.\r\n"
    "tone_keys = wail,yelp,sbs,wig,bumr,sub\r\n"
    "\r\n"
    "# Which vehicle state lights which of the other keys, as key:source pairs. The keys are\r\n"
    "# lrco, warn, dim, flood, radio, sub, sbs, wail, yelp, wig, bumr, leftalley, takedown,\r\n"
    "# rightalley, crimeject and gunlock. The sources are cruise, handbrake, headlights,\r\n"
    "# highbeams, leftsignal, rightsignal, hazards (both signals at once), locked (the doors,\r\n"
    "# as the speedometer's lock icon shows them), engine and none. A key left out stays unlit.\r\n"
    "keys = lrco:handbrake,warn:hazards,dim:headlights,flood:none,radio:none,takedown:highbeams,leftalley:leftsignal,rightalley:rightsignal,crimeject:none,gunlock:locked\r\n"
    "\r\n"
    "# engine: the key legends are backlit only while the engine runs, and dark with it off.\r\n"
    "# always: they are backlit the whole time.\r\n"
    "backlight = engine\r\n"
    "\r\n"
    "# How opaque the panel is, from 0.05 to 1.\r\n"
    "alpha = 1.0\r\n"
    "\r\n"
    "# ---- timing --------------------------------------------------------------------------------\r\n"
    "# How long to wait before retrying a failed connection, and how often to check that the\r\n"
    "# injected panel is still alive. Both in milliseconds.\r\n"
    "reconnect_ms = 2000\r\n"
    "heartbeat_ms = 4000\r\n";

// A comma separated list of the panel's tone keys. One bad entry rejects the whole list rather
// than shifting every tone after it onto the wrong key.
void applyToneKeys(const std::string& value, std::string& target)
{
    std::string out;
    int count = 0;
    for (const std::string& raw : util::split(util::lower(value), ',')) {
        std::string key = util::trim(raw);
        if (key != "sub" && key != "sbs" && key != "wail" && key != "yelp" && key != "wig" &&
            key != "bumr" && key != "none") {
            LOG_WARN("tone_keys: '%s' is not sub, sbs, wail, yelp, wig, bumr or none; keeping %s",
                     key.c_str(), target.c_str());
            return;
        }
        if (++count > 16) break;
        if (!out.empty()) out += ",";
        out += key;
    }
    target = out;
}

// key:source pairs for the panel's vehicle keys. As with the tone keys, one bad pair rejects the
// whole list, so a typo cannot quietly light the wrong key.
bool isOneOf(const std::string& s, const char* const* list)
{
    for (; *list; ++list) if (s == *list) return true;
    return false;
}

void applyPanelKeys(const std::string& value, std::string& target)
{
    static const char* const kKeys[] = { "lrco", "warn", "dim", "flood", "radio", "sub", "sbs",
                                         "wail", "yelp", "wig", "bumr", "leftalley", "takedown",
                                         "rightalley", "crimeject", "gunlock", nullptr };
    static const char* const kSources[] = { "cruise", "handbrake", "headlights", "highbeams",
                                            "leftsignal", "rightsignal", "hazards", "locked",
                                            "engine", "none", nullptr };
    std::string out;
    for (const std::string& raw : util::split(util::lower(value), ',')) {
        std::string pair = util::trim(raw);
        if (pair.empty()) continue;
        size_t colon = pair.find(':');
        std::string key = util::trim(pair.substr(0, colon));
        std::string src = colon == std::string::npos ? std::string() : util::trim(pair.substr(colon + 1));
        if (!isOneOf(key, kKeys) || !isOneOf(src, kSources)) {
            LOG_WARN("keys: '%s' is not a key:source pair this panel has; keeping %s",
                     pair.c_str(), target.c_str());
            return;
        }
        if (!out.empty()) out += ",";
        out += key + ":" + src;
    }
    target = out;
}

std::string readFileUtf8(const std::wstring& path, bool& ok)
{
    ok = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 256 * 1024) { CloseHandle(h); return std::string(); }

    std::string buf(size_t(size.QuadPart), '\0');
    DWORD read = 0;
    if (!buf.empty() && !ReadFile(h, &buf[0], DWORD(buf.size()), &read, nullptr)) {
        CloseHandle(h);
        return std::string();
    }
    CloseHandle(h);
    buf.resize(read);

    // Strip a UTF-8 BOM if an editor added one.
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF)
        buf.erase(0, 3);

    ok = true;
    return buf;
}

}  // namespace

void loadSettings()
{
    std::wstring path = util::modulePath(kSettingsLeaf);
    if (path.empty()) return;

    bool ok = false;
    std::string text = readFileUtf8(path, ok);
    if (!ok) return;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        line = util::trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = util::lower(util::trim(line.substr(0, eq)));
        std::string val = util::trim(line.substr(eq + 1));

        if      (key == "off")          g_set.off         = util::parseBool(val, g_set.off);
        else if (key == "port")         g_set.port        = util::parseInt(val, g_set.port, 1, 65535);
        else if (key == "hold_ms")      g_set.holdMs      = util::parseInt(val, g_set.holdMs, 0, 600000);
        else if (key == "lights_stage") g_set.lightsStage = util::parseInt(val, g_set.lightsStage, 1, 3);
        else if (key == "tone_keys")    applyToneKeys(val, g_set.toneKeys);
        else if (key == "keys")         applyPanelKeys(val, g_set.keys);
        else if (key == "backlight") {
            std::string b = util::lower(val);
            if (b == "engine" || b == "always") g_set.backlight = b;
            else LOG_WARN("backlight: '%s' is not engine or always; keeping %s",
                          val.c_str(), g_set.backlight.c_str());
        }
        else if (key == "alpha")        g_set.alpha       = util::parseDouble(val, g_set.alpha, 0.05, 1.0);
        else if (key == "reconnect_ms") g_set.reconnectMs = util::parseInt(val, g_set.reconnectMs, 250, 120000);
        else if (key == "heartbeat_ms") g_set.heartbeatMs = util::parseInt(val, g_set.heartbeatMs, 500, 120000);

        else LOG_WARN("Unknown setting '%s' ignored", key.c_str());
    }
}

void writeDefaultSettingsIfMissing()
{
    std::wstring path = util::modulePath(kSettingsLeaf);
    if (path.empty()) return;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN("Could not create %ls (err %lu)", kSettingsLeaf, GetLastError());
        return;
    }

    DWORD written = 0;
    DWORD len = DWORD(strlen(kDefaultFile));
    WriteFile(h, kDefaultFile, len, &written, nullptr);
    CloseHandle(h);

    LOG_INFO("Wrote default settings file %ls", kSettingsLeaf);
}
