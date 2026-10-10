#include "cdp/client.h"

#include "core/logger.h"
#include "core/util.h"

#include <cstdio>
#include <string>

namespace cdp {

namespace {

// The page FiveM hosts its whole NUI in, and the frame the GTAW HUD runs in. The HUD's messages
// are delivered to that frame, so that is the one the panel is installed into.
const char* kRootUiUrl    = "nui://game/ui/root.html";
const char* kFramePrefix  = "https://cfx-nui-client/web/";

// A private world, so the injected code shares the page's DOM but not its variables. Nothing the
// plugin defines can collide with, or be reached by, the game's own scripts.
const char* kWorldName    = "gtaw-siren-panel";

// Replies are matched by id. Events and replies to anything else are skipped, but not forever.
const int kMaxSkippedMessages = 64;

bool isLoopbackHost(const std::string& host)
{
    if (host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]")
        return true;

    // Any 127.x.x.x is loopback too.
    if (host.rfind("127.", 0) == 0) {
        for (char c : host)
            if (!((c >= '0' && c <= '9') || c == '.')) return false;
        return true;
    }
    return false;
}

// Depth-first walk of a Page.getFrameTree result, looking for the GTAW client frame.
bool findFrame(const json::Value& node, std::string& id, std::string& url)
{
    const json::Value* frame = node.member("frame");
    if (frame && frame->isObject()) {
        std::string thisUrl = frame->str("url");
        if (thisUrl.rfind(kFramePrefix, 0) == 0) {
            id  = frame->str("id");
            url = thisUrl;
            if (!id.empty()) return true;
        }
    }

    const json::Value* children = node.member("childFrames");
    if (children && children->isArray()) {
        for (const json::Value& child : children->items)
            if (findFrame(child, id, url)) return true;
    }
    return false;
}

}  // namespace

bool parseLoopbackWsUrl(const std::string& url, std::wstring& host, int& port,
                        std::wstring& path, std::string& err)
{
    const std::string scheme = "ws://";
    if (url.rfind(scheme, 0) != 0) {
        err = "debugger endpoint is not a ws:// URL";
        return false;
    }

    size_t authorityStart = scheme.size();
    size_t pathStart = url.find('/', authorityStart);
    std::string authority = (pathStart == std::string::npos)
                          ? url.substr(authorityStart)
                          : url.substr(authorityStart, pathStart - authorityStart);
    std::string pathPart = (pathStart == std::string::npos) ? "/" : url.substr(pathStart);

    // Split host and port, keeping a bracketed IPv6 literal intact.
    std::string hostPart;
    std::string portPart;
    if (!authority.empty() && authority[0] == '[') {
        size_t rb = authority.find(']');
        if (rb == std::string::npos) { err = "malformed IPv6 endpoint"; return false; }
        hostPart = authority.substr(0, rb + 1);
        if (rb + 1 < authority.size() && authority[rb + 1] == ':') portPart = authority.substr(rb + 2);
    } else {
        size_t colon = authority.rfind(':');
        if (colon == std::string::npos) { hostPart = authority; }
        else { hostPart = authority.substr(0, colon); portPart = authority.substr(colon + 1); }
    }

    hostPart = util::lower(hostPart);
    if (!isLoopbackHost(hostPart)) {
        err = "refusing a debugger endpoint that is not on loopback: " + hostPart;
        return false;
    }

    int parsedPort = util::parseInt(portPart, -1, 1, 65535);
    if (parsedPort < 0) { err = "debugger endpoint has no usable port"; return false; }

    // Strip the brackets: WinHttpConnect wants the bare address.
    if (hostPart.size() >= 2 && hostPart.front() == '[' && hostPart.back() == ']')
        hostPart = hostPart.substr(1, hostPart.size() - 2);

    host = util::widen(hostPart);
    port = parsedPort;
    path = util::widen(pathPart);
    return true;
}

bool Client::connect(int port, std::string& err)
{
    close();

    std::string body;
    if (!net::httpGet(L"127.0.0.1", port, L"/json", body, err))
        return false;

    json::Value targets;
    if (!json::parse(body, targets, err)) return false;
    if (!targets.isArray()) { err = "/json did not return a list of targets"; return false; }

    std::string wsUrl;
    for (const json::Value& target : targets.items) {
        if (!target.isObject()) continue;
        if (target.str("url") != kRootUiUrl) continue;
        wsUrl = target.str("webSocketDebuggerUrl");
        if (!wsUrl.empty()) break;
    }
    if (wsUrl.empty()) { err = "FiveM's root UI target was not listed"; return false; }

    std::wstring wsHost, wsPath;
    int wsPort = 0;
    if (!parseLoopbackWsUrl(wsUrl, wsHost, wsPort, wsPath, err)) return false;

    if (!socket_.connect(wsHost, wsPort, wsPath, err)) return false;

    json::Value tree;
    if (!call("Page.getFrameTree", "{}", tree, err)) { close(); return false; }

    const json::Value* rootNode = tree.member("frameTree");
    if (!rootNode || !findFrame(*rootNode, frameId_, frameUrl_)) {
        err = "the GTAW interface frame is not loaded yet";
        close();
        return false;
    }

    // No universal access: the script only ever touches the DOM of the frame it is created in,
    // so the world gets nothing beyond what a script of that frame's own origin would have.
    std::string params = "{\"frameId\":" + json::quote(frameId_) +
                         ",\"worldName\":" + json::quote(kWorldName) + "}";

    json::Value world;
    if (!call("Page.createIsolatedWorld", params, world, err)) { close(); return false; }

    const json::Value* ctx = world.member("executionContextId");
    int contextId = 0;
    if (!ctx || !ctx->asInt(contextId) || contextId == 0) {
        err = "the debugger did not return a usable execution context";
        close();
        return false;
    }

    contextId_ = contextId;
    return true;
}

bool Client::evaluate(const std::string& expression, json::Value& result, std::string& err)
{
    if (!isConnected()) { err = "not connected"; return false; }

    std::string params = "{\"expression\":" + json::quote(expression) +
                         ",\"contextId\":" + std::to_string(contextId_) +
                         ",\"returnByValue\":true,\"awaitPromise\":false,\"silent\":true}";

    json::Value reply;
    if (!call("Runtime.evaluate", params, reply, err)) return false;

    const json::Value* thrown = reply.member("exceptionDetails");
    if (thrown && thrown->isObject()) {
        std::string text = thrown->str("text", "script threw");
        const json::Value* ex = thrown->member("exception");
        if (ex && ex->isObject()) {
            std::string desc = ex->str("description");
            if (!desc.empty()) text = desc;
        }
        err = "injected script failed: " + text;
        return false;
    }

    const json::Value* value = reply.member("result");
    if (value) result = *value;
    else       result = json::Value();
    return true;
}

bool Client::call(const char* method, const std::string& paramsJson, json::Value& result,
                  std::string& err)
{
    if (!socket_.isOpen()) { err = "not connected"; return false; }

    int id = ++nextId_;
    std::string request = "{\"id\":" + std::to_string(id) +
                          ",\"method\":" + json::quote(method) +
                          ",\"params\":" + paramsJson + "}";

    if (!socket_.send(request, err)) return false;

    for (int skipped = 0; skipped < kMaxSkippedMessages; ++skipped) {
        std::string raw;
        if (!socket_.receive(raw, err)) return false;

        json::Value message;
        std::string parseErr;
        if (!json::parse(raw, message, parseErr)) continue;   // not ours to interpret

        const json::Value* replyId = message.member("id");
        int gotId = 0;
        if (!replyId || !replyId->asInt(gotId) || gotId != id) continue;

        const json::Value* failure = message.member("error");
        if (failure && failure->isObject()) {
            err = std::string(method) + ": " + failure->str("message", "protocol error");
            return false;
        }

        const json::Value* payload = message.member("result");
        if (!payload) { err = std::string(method) + ": reply had no result"; return false; }

        result = *payload;
        return true;
    }

    err = std::string(method) + ": no reply";
    return false;
}

void Client::close()
{
    socket_.close();
    contextId_ = 0;
    nextId_    = 0;
    frameId_.clear();
    frameUrl_.clear();
}

}  // namespace cdp
