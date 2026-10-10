#include "core/settings.h"
#include "core/util.h"

#include <cstdio>
#include <string>
#include <vector>

extern std::wstring g_stubModulePath;

// Warnings the parser emitted during the last load, so a test can assert that a file it expects
// to be clean really is.
static std::vector<std::string> g_warnings;

namespace logging {
void init() {}
void shutdown() {}
void write(const char* level, const char* fmt, ...)
{
    if (std::string(level) != "WARN") return;
    char body[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    g_warnings.push_back(body);
}
}  // namespace logging

static int checks = 0, failures = 0;

static void eq(const char* label, const std::string& got, const std::string& want)
{
    ++checks;
    if (got != want) { ++failures; printf("  FAIL %s\n       got:      %s\n       expected: %s\n", label, got.c_str(), want.c_str()); }
    else printf("  ok   %s  =  %s\n", label, got.c_str());
}
static void eq(const char* label, double got, double want)
{
    ++checks;
    if (got != want) { ++failures; printf("  FAIL %s\n       got:      %g\n       expected: %g\n", label, got, want); }
    else printf("  ok   %s  =  %g\n", label, got);
}
static void ok(const char* label, bool cond)
{
    ++checks;
    if (!cond) { ++failures; printf("  FAIL %s\n", label); }
    else printf("  ok   %s\n", label);
}

static std::string dir;

static void writeSettings(const std::string& text)
{
    FILE* f = std::fopen((dir + "/gtaw-siren-panel.settings.txt").c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static void removeSettings() { std::remove((dir + "/gtaw-siren-panel.settings.txt").c_str()); }

static std::string readSettings()
{
    FILE* f = std::fopen((dir + "/gtaw-siren-panel.settings.txt").c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

static void reload()
{
    g_set = Settings();
    g_warnings.clear();
    loadSettings();
}

int main(int argc, char** argv)
{
    dir = (argc > 1) ? argv[1] : ".";
    std::wstring wdir = util::widen(dir);
    g_stubModulePath = wdir + L"\\gtaw-siren-panel.asi";

    printf("\n== the default file this plugin writes is understood by its own parser ==\n");
    removeSettings();
    writeDefaultSettingsIfMissing();
    std::string defaults = readSettings();
    ok("a default file was written", !defaults.empty());

    reload();
    if (!g_warnings.empty())
        for (const std::string& w : g_warnings) printf("       warning: %s\n", w.c_str());
    ok("it parses with no warnings", g_warnings.empty());

    // Every value in the shipped file must equal the struct default, or the file is lying about
    // what the plugin will do.
    Settings fresh;
    eq("off matches",          g_set.off ? "yes" : "no", fresh.off ? "yes" : "no");
    eq("port matches",         double(g_set.port), double(fresh.port));
    eq("hold_ms matches",      double(g_set.holdMs), double(fresh.holdMs));
    eq("lights_stage matches", double(g_set.lightsStage), double(fresh.lightsStage));
    eq("tone_keys matches",    g_set.toneKeys, fresh.toneKeys);
    eq("keys matches",         g_set.keys, fresh.keys);
    eq("backlight matches",    g_set.backlight, fresh.backlight);
    eq("alpha matches",        g_set.alpha, fresh.alpha);
    eq("reconnect_ms matches", double(g_set.reconnectMs), double(fresh.reconnectMs));
    eq("heartbeat_ms matches", double(g_set.heartbeatMs), double(fresh.heartbeatMs));
    ok("the panel stays up by default", fresh.holdMs == 0);
    ok("GUN LOCK follows the door locks by default",
       fresh.keys.find("gunlock:locked") != std::string::npos);

    printf("\n== writeDefaultSettingsIfMissing does not clobber an existing file ==\n");
    writeSettings("# mine\r\nhold_ms = 5000\r\n");
    writeDefaultSettingsIfMissing();
    ok("the file was left alone", readSettings() == "# mine\r\nhold_ms = 5000\r\n");

    printf("\n== values are read ==\n");
    writeSettings(
        "off = yes\n"
        "port=9222\n"
        "  hold_ms   =   5000  \n"
        "lights_stage = 3\n"
        "tone_keys = YELP, wig ,none\n"
        "alpha = 0.5\n"
        "# a comment = ignored\n"
        "\n");
    reload();
    ok("off",          g_set.off == true);
    eq("port",         double(g_set.port), 9222.0);
    eq("hold",         double(g_set.holdMs), 5000.0);
    eq("lights stage", double(g_set.lightsStage), 3.0);
    eq("tone keys trimmed and lowered", g_set.toneKeys, "yelp,wig,none");
    eq("alpha",        g_set.alpha, 0.5);
    ok("no warnings for a valid file", g_warnings.empty());

    writeSettings("keys = GunLock : Locked, warn:Hazards, dim:none,\nbacklight = Always\n");
    reload();
    eq("keys trimmed and lowered", g_set.keys, "gunlock:locked,warn:hazards,dim:none");
    eq("backlight", g_set.backlight, "always");
    ok("no warnings for the key list", g_warnings.empty());

    printf("\n== anything else keeps the default ==\n");
    writeSettings("keys = lrco:handbrake,park:headlights\nbacklight = sometimes\n");
    reload();
    eq("a key the panel cannot light refuses the list", g_set.keys, fresh.keys);
    eq("an unknown backlight mode is refused", g_set.backlight, fresh.backlight);
    ok("both warned", g_warnings.size() == 2);

    writeSettings("keys = lrco:horn\n");
    reload();
    eq("a source the game does not report refuses the list", g_set.keys, fresh.keys);

    writeSettings("keys = stby:cruise\n");
    reload();
    eq("STBY is the siren's own, not a key to give a source", g_set.keys, fresh.keys);

    writeSettings("lights_stage = 4\ntone_keys = wail,prty,yelp\nalpha = 0\nhold_ms = -1\n"
                  "port = 70000\nheartbeat_ms = 10\n");
    reload();
    eq("a stage past 3 is refused", double(g_set.lightsStage), double(fresh.lightsStage));
    eq("one unknown key refuses the whole list", g_set.toneKeys, fresh.toneKeys);
    eq("an invisible panel is refused", g_set.alpha, fresh.alpha);
    eq("a negative hold is refused", double(g_set.holdMs), double(fresh.holdMs));
    eq("port out of range", double(g_set.port), double(fresh.port));
    eq("heartbeat too small", double(g_set.heartbeatMs), double(fresh.heartbeatMs));

    printf("\n== odds and ends ==\n");
    writeSettings("\xEF\xBB\xBF" "hold_ms = 2500\n");
    reload();
    eq("a byte order mark is stripped", double(g_set.holdMs), 2500.0);

    writeSettings("not_a_setting = 1\nshow_cash = no\n");
    reload();
    ok("unknown keys warn, the old HUD's among them", g_warnings.size() == 2);

    removeSettings();

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
