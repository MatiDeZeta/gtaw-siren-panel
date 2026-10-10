#include "cdp/json.h"

#include <cstdio>
#include <string>

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
    if (got != want) {
        ++failures;
        printf("  FAIL %s\n       got:      %s\n       expected: %s\n",
               label, got.c_str(), want.c_str());
    } else {
        printf("  ok   %s  =  %s\n", label, got.c_str());
    }
}

static bool rejects(const std::string& text)
{
    json::Value v; std::string err;
    return !json::parse(text, v, err);
}

int main()
{
    std::string err;

    printf("\n== a targets list, the shape /json returns ==\n");
    {
        json::Value v;
        const char* body =
            "[{\"description\":\"\",\"id\":\"A\",\"title\":\"root\","
            "\"type\":\"page\",\"url\":\"nui://game/ui/root.html\","
            "\"webSocketDebuggerUrl\":\"ws://127.0.0.1:13172/devtools/page/A\"},"
            "{\"url\":\"https://cfx-nui-chat/index.html\"}]";
        ok("parses", json::parse(body, v, err));
        ok("is an array", v.isArray() && v.items.size() == 2);
        eq("target url", v.items[0].str("url"), "nui://game/ui/root.html");
        eq("socket url", v.items[0].str("webSocketDebuggerUrl"), "ws://127.0.0.1:13172/devtools/page/A");
        eq("missing key is empty", v.items[1].str("webSocketDebuggerUrl"), "");
    }

    printf("\n== a frame tree ==\n");
    {
        json::Value v;
        const char* body =
            "{\"frameTree\":{\"frame\":{\"id\":\"root\",\"url\":\"nui://game/ui/root.html\"},"
            "\"childFrames\":[{\"frame\":{\"id\":\"F2\",\"url\":\"https://cfx-nui-client/web/index.html\"}}]}}";
        ok("parses", json::parse(body, v, err));
        const json::Value* tree = v.member("frameTree");
        ok("frameTree present", tree && tree->isObject());
        const json::Value* kids = tree->member("childFrames");
        ok("one child", kids && kids->isArray() && kids->items.size() == 1);
        const json::Value* frame = kids->items[0].member("frame");
        eq("child frame url", frame->str("url"), "https://cfx-nui-client/web/index.html");
    }

    printf("\n== values ==\n");
    {
        json::Value v;
        ok("parses", json::parse("{\"a\":1.5,\"b\":true,\"c\":null,\"d\":-2e3,\"e\":\"x\"}", v, err));
        ok("number", v.num("a") == 1.5);
        ok("bool",   v.flag("b") == true);
        ok("null",   v.member("c") && v.member("c")->isNull());
        ok("exponent", v.num("d") == -2000.0);
        eq("string",   v.str("e"), "x");
        ok("wrong type falls back", v.num("e", 42.0) == 42.0);
        ok("absent falls back",     v.flag("zz", true) == true);
    }

    printf("\n== escapes ==\n");
    {
        json::Value v;
        ok("parses", json::parse("[\"a\\\"b\",\"\\u00e9\",\"\\ud83d\\ude00\",\"\\u0041\",\"tab\\there\"]", v, err));
        eq("quote",           v.items[0].string, "a\"b");
        eq("two byte utf8",   v.items[1].string, "\xc3\xa9");
        eq("surrogate pair",  v.items[2].string, "\xf0\x9f\x98\x80");
        eq("ascii escape",    v.items[3].string, "A");
        eq("control escape",  v.items[4].string, "tab\there");
    }
    {
        // A lone surrogate must not produce invalid UTF-8 that later goes into a stylesheet.
        json::Value v;
        ok("lone surrogate parses", json::parse("[\"\\ud83d\"]", v, err));
        eq("becomes U+FFFD", v.items[0].string, "\xef\xbf\xbd");
    }

    printf("\n== malformed input is refused ==\n");
    ok("unterminated string",  rejects("{\"a\":\"x}"));
    ok("trailing data",        rejects("{} {}"));
    ok("trailing comma",       rejects("[1,]"));
    ok("bare word",            rejects("nope"));
    ok("missing colon",        rejects("{\"a\" 1}"));
    ok("unclosed array",       rejects("[1,2"));
    ok("raw control char",     rejects("[\"a\nb\"]"));
    ok("bad unicode escape",   rejects("[\"\\uZZZZ\"]"));
    ok("unknown escape",       rejects("[\"\\q\"]"));
    ok("empty input",          rejects(""));
    ok("lone minus",           rejects("[-]"));

    printf("\n== numbers stay in a range the rest of the plugin can use ==\n");
    // Every protocol number eventually becomes an int (a request id, a context id). Converting
    // an out-of-range double to int is undefined behaviour, so the range is enforced here.
    ok("overflow to infinity is refused",  rejects("1e309"));
    ok("negative overflow is refused",     rejects("[-1e309]"));
    ok("underflow to zero is fine",        !rejects("1e-400"));
    {
        json::Value v; int n = 0;
        ok("parses", json::parse("{\"id\":7,\"big\":1e300,\"frac\":1.5,\"neg\":-3,\"txt\":\"7\"}", v, err));
        ok("a plain id reads back",        v.member("id")->asInt(n) && n == 7);
        ok("a huge value is refused",      !v.member("big")->asInt(n));
        ok("a negative value reads back",  v.member("neg")->asInt(n) && n == -3);
        ok("a fraction truncates",         v.member("frac")->asInt(n) && n == 1);
        ok("a string is not an int",       !v.member("txt")->asInt(n));
        ok("int range edges",
           json::parse("[2147483647,-2147483648,2147483648]", v, err) &&
           v.items[0].asInt(n) && n == 2147483647 &&
           v.items[1].asInt(n) && n == -2147483648 &&
           !v.items[2].asInt(n));
    }

    printf("\n== nesting is capped ==\n");
    {
        std::string deep;
        for (int i = 0; i < 40; ++i) deep += "[";
        deep += "1";
        for (int i = 0; i < 40; ++i) deep += "]";
        json::Value v;
        ok("40 deep is fine", json::parse(deep, v, err));

        std::string tooDeep;
        for (int i = 0; i < 500; ++i) tooDeep += "[";
        for (int i = 0; i < 500; ++i) tooDeep += "]";
        ok("500 deep is refused", rejects(tooDeep));
    }

    printf("\n== empty containers ==\n");
    {
        json::Value v;
        ok("empty object", json::parse("{}", v, err) && v.isObject() && v.members.empty());
        ok("empty array",  json::parse("[]", v, err) && v.isArray()  && v.items.empty());
        ok("nested empty", json::parse("{\"a\":{},\"b\":[]}", v, err));
    }

    printf("\n== quote() ==\n");
    eq("plain",        json::quote("abc"), "\"abc\"");
    eq("quote and backslash", json::quote("a\"b\\c"), "\"a\\\"b\\\\c\"");
    eq("newline",      json::quote("a\nb"), "\"a\\nb\"");
    eq("control char", json::quote(std::string("a\x01""b")), "\"a\\u0001b\"");
    eq("utf8 passes through", json::quote("\xc3\xa9"), "\"\xc3\xa9\"");
    // U+2028 is a plain character in JSON but not in a JavaScript string literal, and this
    // output is pasted into one.
    eq("U+2028 escaped", json::quote("a\xe2\x80\xa8""b"), "\"a\\u2028b\"");
    eq("U+2029 escaped", json::quote("a\xe2\x80\xa9""b"), "\"a\\u2029b\"");

    printf("\n== round trip through quote() and back ==\n");
    {
        std::string nasty = "line\nbreak \"quoted\" \\slash\\ \xc3\xa9 \xf0\x9f\x98\x80 \x01";
        std::string doc = "{\"v\":" + json::quote(nasty) + "}";
        json::Value v;
        ok("re-parses", json::parse(doc, v, err));
        eq("value survives", v.str("v"), nasty);
    }

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
