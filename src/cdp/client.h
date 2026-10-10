#pragma once

#include "cdp/json.h"
#include "cdp/transport.h"

#include <string>

namespace cdp {

// Splits a ws:// URL into its parts and refuses anything that is not on loopback.
//
// This URL is the one value in the whole path that the plugin does not choose for itself: it is
// read back out of the debugger's own JSON. That JSON comes from localhost, but nothing in the
// protocol promises the address inside it points there too, and this plugin is meant to make no
// outbound connection at all. So the guarantee is enforced rather than assumed.
bool parseLoopbackWsUrl(const std::string& url, std::wstring& host, int& port,
                        std::wstring& path, std::string& err);

class Client
{
public:
    // Finds the FiveM root UI target, opens the socket, locates the GTAW client frame and
    // creates a private execution context in it.
    bool connect(int port, std::string& err);

    // Runs an expression in that context. Returns false if the call failed, the context is gone,
    // or the expression threw.
    bool evaluate(const std::string& expression, json::Value& result, std::string& err);

    void close();
    bool isConnected() const { return socket_.isOpen() && contextId_ != 0; }

    const std::string& frameUrl() const { return frameUrl_; }

private:
    bool call(const char* method, const std::string& paramsJson, json::Value& result,
              std::string& err);

    net::WebSocket socket_;
    int            contextId_ = 0;
    int            nextId_    = 0;
    std::string    frameId_;
    std::string    frameUrl_;
};

}  // namespace cdp
