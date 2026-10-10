// Stand-ins for the WinHTTP transport and the log, so the URL guard in client.cpp can be
// compiled and exercised on Linux.
#include "cdp/transport.h"
#include "core/logger.h"

namespace net {

bool httpGet(const std::wstring&, int, const std::wstring&, std::string&, std::string& err)
{
    err = "stubbed";
    return false;
}

WebSocket::~WebSocket() {}
bool WebSocket::connect(const std::wstring&, int, const std::wstring&, std::string& err) { err = "stubbed"; return false; }
bool WebSocket::send(const std::string&, std::string& err) { err = "stubbed"; return false; }
bool WebSocket::receive(std::string&, std::string& err)    { err = "stubbed"; return false; }
void WebSocket::close() {}

}  // namespace net

namespace logging {
void init() {}
void write(const char*, const char*, ...) {}
void shutdown() {}
}  // namespace logging
