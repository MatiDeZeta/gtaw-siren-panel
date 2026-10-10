#include "core/worker.h"

#include "cdp/client.h"
#include "core/layout.h"
#include "core/logger.h"
#include "core/settings.h"
#include "core/util.h"
#include "core/version.h"
#include "hud/payload.h"

#include <string>

namespace {

HANDLE g_stop = nullptr;

// Returns false when a shutdown was requested during the wait.
bool sleepOrStop(DWORD ms)
{
    if (!g_stop) { Sleep(ms); return true; }
    return WaitForSingleObject(g_stop, ms) == WAIT_TIMEOUT;
}

// Runtime.evaluate answers with {"type":...,"value":...}; this reads the value out of it.
std::string resultText(const json::Value& result)
{
    const json::Value* value = result.member("value");
    if (value && value->isString()) return value->string;
    return "ok";
}

}  // namespace

bool createStopEvent()
{
    if (g_stop) return true;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return g_stop != nullptr;
}

void requestStop()
{
    if (g_stop) SetEvent(g_stop);
}

DWORD WINAPI HudWorker(LPVOID)
{
    logging::init();
    writeDefaultSettingsIfMissing();
    loadSettings();

    if (g_set.off) {
        LOG_INFO("off = yes in the settings file, so nothing will be drawn.");
        logging::shutdown();
        return 0;
    }

    LOG_INFO("gtaw-siren-panel %s waiting for the interface on 127.0.0.1:%d",
             GTAW_SIREN_PANEL_VERSION, g_set.port);

    // The LEO edition of gtaw-oldhud draws this same panel, so with both loaded it would be drawn
    // twice. Nothing is taken away from the other one; it is only pointed out.
    std::wstring leo = util::modulePath(L"gtaw-oldhud-leo.asi");
    if (!leo.empty() && GetFileAttributesW(leo.c_str()) != INVALID_FILE_ATTRIBUTES)
        LOG_WARN("gtaw-oldhud-leo.asi is in the plugins folder too, and it draws this panel "
                 "already, so it will be drawn twice. Keep one of the two.");

    cdp::Client client;
    bool        installed = false;

    // The interface is not up for the first stretch of every launch, so the same "not ready yet"
    // reason would otherwise be written every two seconds for minutes. Only changes are logged.
    std::string lastQuietReason;

    for (;;) {
        DWORD delay = (DWORD)g_set.heartbeatMs;

        if (!client.isConnected()) {
            std::string err;
            if (client.connect(g_set.port, err)) {
                LOG_INFO("Attached to %s", client.frameUrl().c_str());
                lastQuietReason.clear();
                installed = false;
            } else {
                if (err != lastQuietReason) {
                    LOG_INFO("Waiting: %s", err.c_str());
                    lastQuietReason = err;
                }
                installed = false;
                delay = (DWORD)g_set.reconnectMs;
            }
        }

        if (client.isConnected() && !installed) {
            std::string  err;
            json::Value  result;
            if (client.evaluate(hud::buildInstallScript(), result, err)) {
                // The script answers "installed <version>" once its nodes are on the page. The
                // only other answer is "no-body": the frame is there but its document is not
                // built yet, which is a wait, not a failure.
                std::string answer = resultText(result);
                if (answer.rfind("installed", 0) == 0) {
                    LOG_INFO("Panel installed (%s)", answer.c_str());
                    installed = true;
                } else {
                    if (answer != lastQuietReason) {
                        LOG_INFO("Waiting: the interface document is not ready (%s)", answer.c_str());
                        lastQuietReason = answer;
                    }
                    delay = (DWORD)g_set.reconnectMs;
                }
            } else {
                LOG_WARN("Could not install the panel: %s", err.c_str());
                client.close();
                delay = (DWORD)g_set.reconnectMs;
            }
        } else if (client.isConnected()) {
            std::string  err;
            json::Value  result;
            if (!client.evaluate(hud::buildProbeScript(), result, err)) {
                // Usually the document was replaced: a reconnect, a resource restart, or the
                // interface reloading. The private world went with it, so start again.
                LOG_INFO("Lost the interface (%s); reattaching", err.c_str());
                client.close();
                installed = false;
                delay = (DWORD)g_set.reconnectMs;
            } else {
                const json::Value* value = result.member("value");
                std::string answer = (value && value->isString()) ? value->string : std::string();
                if (answer.rfind("ok", 0) != 0) {
                    LOG_INFO("The panel is no longer on the page; reinstalling");
                    installed = false;
                } else if (answer.size() >= 3 && answer[2] == '\n') {
                    // The layout editor placed the panel; keep the result next to the .asi.
                    // Empty is a Reset, and is kept too.
                    layout::save(answer.substr(3));
                }
            }
        }

        if (!sleepOrStop(delay)) break;
    }

    client.close();
    LOG_INFO("Stopping.");
    logging::shutdown();
    return 0;
}
