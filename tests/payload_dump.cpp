// Prints the exact expression the plugin would hand to Runtime.evaluate, so the real generated
// config and stylesheet can be exercised instead of a hand-written copy of them.
#include "core/logger.h"
#include "core/settings.h"
#include "hud/payload.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace logging {
void init() {}
void write(const char*, const char*, ...) {}
void shutdown() {}
}  // namespace logging

int main(int argc, char** argv)
{
    // Let a test ask for a non-default configuration without needing a settings file.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hold") == 0) g_set.holdMs = 5000;
    }

    std::string script = hud::buildInstallScript();
    fwrite(script.data(), 1, script.size(), stdout);
    return 0;
}
