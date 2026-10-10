#include "cdp/client.h"
#include "core/util.h"

#include <cstdio>
#include <string>

static int checks = 0, failures = 0;

static void accepts(const char* url, const char* wantHost, int wantPort, const char* wantPath)
{
    ++checks;
    std::wstring host, path; int port = 0; std::string err;
    bool ok = cdp::parseLoopbackWsUrl(url, host, port, path, err);

    std::string gotHost = util::narrow(host);
    std::string gotPath = util::narrow(path);

    if (!ok) { ++failures; printf("  FAIL accept %-46s  refused: %s\n", url, err.c_str()); return; }
    if (gotHost != wantHost || port != wantPort || gotPath != wantPath) {
        ++failures;
        printf("  FAIL accept %-46s  got %s:%d%s\n", url, gotHost.c_str(), port, gotPath.c_str());
        return;
    }
    printf("  ok   accept %-46s -> %s:%d%s\n", url, gotHost.c_str(), port, gotPath.c_str());
}

static void refuses(const char* url, const char* why)
{
    ++checks;
    std::wstring host, path; int port = 0; std::string err;
    if (cdp::parseLoopbackWsUrl(url, host, port, path, err)) {
        ++failures;
        printf("  FAIL refuse %-46s  was ACCEPTED as %s:%d\n", url, util::narrow(host).c_str(), port);
        return;
    }
    printf("  ok   refuse %-46s (%s)\n", url, why);
}

int main()
{
    printf("\n== loopback endpoints are accepted ==\n");
    accepts("ws://127.0.0.1:13172/devtools/page/A1B2", "127.0.0.1", 13172, "/devtools/page/A1B2");
    accepts("ws://localhost:13172/devtools/page/X",    "localhost", 13172, "/devtools/page/X");
    accepts("ws://LOCALHOST:13172/x",                  "localhost", 13172, "/x");
    accepts("ws://127.0.0.53:9222/x",                  "127.0.0.53", 9222, "/x");
    accepts("ws://[::1]:13172/devtools/page/A",        "::1",       13172, "/devtools/page/A");
    accepts("ws://127.0.0.1:13172",                    "127.0.0.1", 13172, "/");

    printf("\n== anything else is refused ==\n");
    refuses("ws://10.0.0.5:13172/x",            "private LAN address");
    refuses("ws://192.168.1.9:13172/x",         "private LAN address");
    refuses("ws://8.8.8.8:13172/x",             "public address");
    refuses("ws://evil.example.com:13172/x",    "a name, not loopback");
    refuses("ws://127.0.0.1.evil.com:13172/x",  "prefix that only looks like loopback");
    refuses("ws://127.0.0.1evil:13172/x",       "loopback prefix with a suffix");
    refuses("ws://user@evil.com:13172/x",       "userinfo hiding the real host");
    refuses("ws://127.0.0.1:13172@evil.com/x",  "host that is really userinfo");
    refuses("wss://127.0.0.1:13172/x",          "wrong scheme");
    refuses("http://127.0.0.1:13172/x",         "wrong scheme");
    refuses("ws://127.0.0.1:0/x",               "port 0");
    refuses("ws://127.0.0.1:70000/x",           "port out of range");
    refuses("ws://127.0.0.1:abc/x",             "non-numeric port");
    refuses("ws://127.0.0.1/x",                 "no port");
    refuses("ws://[::1:13172/x",                "unterminated IPv6 literal");
    refuses("ws://[2001:db8::1]:13172/x",       "a real IPv6 address, not ::1");
    refuses("",                                 "empty");
    refuses("ws://",                            "no authority");

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
