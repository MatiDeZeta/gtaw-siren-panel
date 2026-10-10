#include "cdp/transport.h"

#include "core/logger.h"

#include <cstdio>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace net {

namespace {

const wchar_t* kAgent = L"gtaw-siren-panel";

// Generous enough for a frame tree, small enough that a runaway peer cannot exhaust memory.
const size_t kMaxMessageBytes = 8u * 1024u * 1024u;
// WinHttpSetTimeouts takes ints, so these are ints.
const int kResolveTimeout = 3000;
const int kConnectTimeout = 3000;
const int kSendTimeout    = 5000;
const int kReceiveTimeout = 8000;

std::string winErr(const char* what, DWORD code)
{
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s failed (%lu)", what, code);
    return buf;
}

HINTERNET openSession()
{
    HINTERNET s = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) WinHttpSetTimeouts(s, kResolveTimeout, kConnectTimeout, kSendTimeout, kReceiveTimeout);
    return s;
}

}  // namespace

bool httpGet(const std::wstring& host, int port, const std::wstring& path,
             std::string& body, std::string& err)
{
    body.clear();

    HINTERNET session = openSession();
    if (!session) { err = winErr("WinHttpOpen", GetLastError()); return false; }

    HINTERNET connection = WinHttpConnect(session, host.c_str(), (INTERNET_PORT)port, 0);
    if (!connection) {
        err = winErr("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    bool ok = false;
    do {
        if (!request) { err = winErr("WinHttpOpenRequest", GetLastError()); break; }

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            err = winErr("WinHttpSendRequest", GetLastError());
            break;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            err = winErr("WinHttpReceiveResponse", GetLastError());
            break;
        }

        DWORD status = 0, statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX) && status != 200) {
            char buf[64];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "HTTP %lu", status);
            err = buf;
            break;
        }

        char chunk[8192];
        bool readFailed = false;
        for (;;) {
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk, sizeof(chunk), &read)) {
                err = winErr("WinHttpReadData", GetLastError());
                readFailed = true;
                break;
            }
            if (read == 0) break;                       // end of the body
            if (body.size() + read > kMaxMessageBytes) {
                err = "response too large";
                readFailed = true;
                break;
            }
            body.append(chunk, read);
        }
        if (readFailed) break;

        ok = true;
    } while (false);

    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

WebSocket::~WebSocket()
{
    close();
}

bool WebSocket::connect(const std::wstring& host, int port, const std::wstring& path,
                        std::string& err)
{
    close();

    session_ = openSession();
    if (!session_) { err = winErr("WinHttpOpen", GetLastError()); return false; }

    connect_ = WinHttpConnect(session_, host.c_str(), (INTERNET_PORT)port, 0);
    if (!connect_) { err = winErr("WinHttpConnect", GetLastError()); close(); return false; }

    HINTERNET request = WinHttpOpenRequest(connect_, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { err = winErr("WinHttpOpenRequest", GetLastError()); close(); return false; }

    // Must be set before the request is sent; this is what turns the response into an upgrade.
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        err = winErr("WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET", GetLastError());
        WinHttpCloseHandle(request);
        close();
        return false;
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        err = winErr("WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(request);
        close();
        return false;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        err = winErr("WinHttpReceiveResponse", GetLastError());
        WinHttpCloseHandle(request);
        close();
        return false;
    }

    socket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
    DWORD upgradeErr = GetLastError();

    // The request handle is finished with either way once the upgrade has been completed.
    WinHttpCloseHandle(request);

    if (!socket_) {
        err = winErr("WinHttpWebSocketCompleteUpgrade", upgradeErr);
        close();
        return false;
    }
    return true;
}

bool WebSocket::send(const std::string& text, std::string& err)
{
    if (!socket_) { err = "socket is not connected"; return false; }

    DWORD rc = WinHttpWebSocketSend(socket_,
                                    WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                    text.empty() ? nullptr : (PVOID)text.data(),
                                    (DWORD)text.size());
    if (rc != NO_ERROR) { err = winErr("WinHttpWebSocketSend", rc); return false; }
    return true;
}

bool WebSocket::receive(std::string& out, std::string& err)
{
    out.clear();
    if (!socket_) { err = "socket is not connected"; return false; }

    char chunk[16384];
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

        DWORD rc = WinHttpWebSocketReceive(socket_, chunk, (DWORD)sizeof(chunk), &read, &type);
        if (rc != NO_ERROR) { err = winErr("WinHttpWebSocketReceive", rc); return false; }

        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            err = "the debugger endpoint closed the connection";
            return false;
        }

        if (out.size() + read > kMaxMessageBytes) { err = "message too large"; return false; }
        out.append(chunk, read);

        // Anything that is not a FRAGMENT type is the final piece of the message.
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
            return true;
    }
}

void WebSocket::close()
{
    if (socket_) {
        WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(socket_);
        socket_ = nullptr;
    }
    if (connect_) { WinHttpCloseHandle(connect_); connect_ = nullptr; }
    if (session_) { WinHttpCloseHandle(session_); session_ = nullptr; }
}

}  // namespace net
