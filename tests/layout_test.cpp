// The layout file: what the injected script hands back through the heartbeat is validated
// before it is written next to the .asi, and read back the same way.
//
// Usage: layout_test <scratch-dir>
#include "core/layout.h"
#include "core/util.h"

#include <cstdio>
#include <string>
#include <vector>

extern std::wstring g_stubModulePath;

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

static void ok(const char* label, bool cond)
{
    ++checks;
    if (!cond) { ++failures; printf("  FAIL %s\n", label); }
    else printf("  ok   %s\n", label);
}
static void eq(const char* label, const std::string& got, const std::string& want)
{
    ++checks;
    if (got != want) { ++failures; printf("  FAIL %s\n       got:      [%s]\n       expected: [%s]\n", label, got.c_str(), want.c_str()); }
    else printf("  ok   %s\n", label);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: layout_test <scratch-dir>\n"); return 2; }
    std::string dir = argv[1];
    g_stubModulePath = util::widen(dir) + L"\\gtaw-siren-panel.asi";
    std::remove((dir + "/gtaw-siren-panel.layout.txt").c_str());

    printf("\n== what the script writes validates ==\n");
    const std::string good = "panel = 0.01250 -0.02778 1.25 0\n";
    ok("a layout in the file's shape", layout::isValid(good));
    ok("an empty layout, which is a Reset", layout::isValid(""));
    ok("blank lines and CR are fine", layout::isValid("\r\n\r\npanel = 0 0 1 0\r\n"));

    printf("\n== anything else does not ==\n");
    ok("an unknown block", !layout::isValid("chat = 0 0 1 0"));
    ok("a fifth field", !layout::isValid("panel = 0 0 1 0 0"));
    ok("three fields", !layout::isValid("panel = 0 0 1"));
    ok("hidden that is not 0 or 1", !layout::isValid("panel = 0 0 1 yes"));
    ok("a scale outside the range", !layout::isValid("panel = 0 0 9 0"));
    ok("an offset outside the screen", !layout::isValid("panel = 2 0 1 0"));
    ok("an exponent", !layout::isValid("panel = 1e-3 0 1 0"));
    ok("no equals sign", !layout::isValid("panel 0 0 1 0"));
    ok("markup", !layout::isValid("panel = 0 0 1 0 <script>"));
    std::string huge(5000, 'x');
    ok("anything too long", !layout::isValid(huge));

    printf("\n== round trip ==\n");
    eq("nothing on disk reads as empty", layout::load(), "");
    ok("a valid layout is written", layout::save(good));
    eq("and read back", layout::load(),
       "panel = 0.01250 -0.02778 1.25 0\n");
    ok("an invalid one is refused", !layout::save("panel = 0 0 1 0 <script>"));
    eq("and the file is untouched", layout::load(),
       "panel = 0.01250 -0.02778 1.25 0\n");
    ok("a Reset writes an empty file", layout::save(""));
    eq("which reads as empty", layout::load(), "");

    printf("\n== a file someone else edited ==\n");
    FILE* f = std::fopen((dir + "/gtaw-siren-panel.layout.txt").c_str(), "wb");
    std::fputs("panel = 0 0 1 0\nevil = 1 1 1 1\n", f);
    std::fclose(f);
    g_warnings.clear();
    eq("is ignored whole", layout::load(), "");
    ok("with a warning", !g_warnings.empty());

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
