#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>

// HTTP and WebSocket over WinHTTP, which is part of Windows. Nothing is vendored and nothing is
// downloaded, so there is no third-party code in the plugin's network path.
//
// Every call here is made with WINHTTP_ACCESS_TYPE_NO_PROXY. The endpoint is on loopback, and a
// configured system proxy must not be able to see or redirect it.
namespace net {

// Plain GET, expecting a small body. Returns false and fills err on any failure.
bool httpGet(const std::wstring& host, int port, const std::wstring& path,
             std::string& body, std::string& err);

class WebSocket
{
public:
    WebSocket() = default;
    ~WebSocket();

    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    bool connect(const std::wstring& host, int port, const std::wstring& path, std::string& err);
    bool send(const std::string& text, std::string& err);

    // Reassembles fragments into one whole message. Blocks up to the receive timeout set on
    // the session.
    bool receive(std::string& out, std::string& err);

    void close();
    bool isOpen() const { return socket_ != nullptr; }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    HINTERNET socket_  = nullptr;
};

}  // namespace net
