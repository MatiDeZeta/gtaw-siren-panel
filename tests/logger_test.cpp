#include "core/logger.h"

#include <cstdio>
#include <string>

extern std::wstring g_stubModulePath;

int main(int argc, char** argv)
{
    if (argc != 2) return 2;

    std::string logPath = std::string(argv[1]) + "/gtaw-siren-panel.log";
    std::remove(logPath.c_str());
    std::wstring dir;
    for (const char* c = argv[1]; *c; ++c) dir.push_back(wchar_t(*c));
    g_stubModulePath = dir + L"\\gtaw-siren-panel.asi";

    logging::init();
    LOG_INFO("ready");
    logging::shutdown();

    FILE* file = std::fopen(logPath.c_str(), "rb");
    if (!file) { std::puts("FAIL no log file"); return 1; }
    std::string contents;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), file)) != 0) contents.append(buf, n);
    std::fclose(file);

    if (contents.find("----- gtaw-siren-panel starting -----") == std::string::npos ||
        contents.find("ready") == std::string::npos ||
        contents.find('\0') != std::string::npos) {
        std::printf("FAIL startup lines missing or not byte text (%zu bytes)\n", contents.size());
        return 1;
    }
    std::printf("ok startup and next line written (%zu bytes)\n", contents.size());
    return 0;
}
