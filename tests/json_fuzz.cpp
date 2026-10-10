// Mutation fuzzer for the JSON reader, built with AddressSanitizer and UndefinedBehaviorSanitizer.
//
// The reader is the only component that parses bytes the plugin did not produce itself. Those
// bytes normally come from FiveM's own debugging endpoint, but nothing guarantees that: if FiveM
// is not running, any local process can bind 127.0.0.1:13172 first and answer with whatever it
// likes. So the reader is treated as facing hostile input and fuzzed accordingly.
//
// Anything parsed is then walked the way the real code walks it, so a bad value that only bites
// on access is caught too.
#include "cdp/json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// Shapes the plugin actually receives, plus the awkward corners of the format.
const char* kSeeds[] = {
    "{}", "[]", "null", "true", "false", "0", "-1.5e10", "\"x\"", "\"\"",
    "{\"a\":[1,2,{\"b\":null}],\"c\":\"\\u00e9\"}",
    "[{\"id\":\"A\",\"type\":\"page\",\"url\":\"nui://game/ui/root.html\","
      "\"webSocketDebuggerUrl\":\"ws://127.0.0.1:13172/devtools/page/A\"}]",
    "{\"frameTree\":{\"frame\":{\"id\":\"A\",\"url\":\"nui://game/ui/root.html\"},"
      "\"childFrames\":[{\"frame\":{\"id\":\"B\",\"url\":\"https://cfx-nui-client/web/index.html\"},"
      "\"childFrames\":[]}]}}",
    "{\"id\":1,\"result\":{\"executionContextId\":7}}",
    "{\"id\":1,\"result\":{\"result\":{\"type\":\"string\",\"value\":\"installed 0.1.0\"}}}",
    "{\"id\":1,\"result\":{\"result\":{\"type\":\"boolean\",\"value\":true}}}",
    "{\"id\":1,\"error\":{\"code\":-32000,\"message\":\"Cannot find context with specified id\"}}",
    "{\"id\":1,\"result\":{\"exceptionDetails\":{\"text\":\"Uncaught\","
      "\"exception\":{\"description\":\"TypeError: x\"}}}}",
    "{\"method\":\"Page.frameNavigated\",\"params\":{\"frame\":{\"id\":\"A\"}}}",
    "\"\\ud83d\\ude00\"", "\"\\ud83d\"", "\"\\udc00\"", "\"\\u0000\"",
    "[1e309,-1e309,1e-400]",
    "\"\\\\\\\\\\\"\"",
};
const size_t kSeedCount = sizeof(kSeeds) / sizeof(kSeeds[0]);

// Walks a parsed value the way the plugin does, touching every accessor.
long long walk(const json::Value& v, int depth)
{
    if (depth > 200) return 0;
    long long acc = (long long)v.type;

    switch (v.type) {
        case json::Type::String: acc += (long long)v.string.size(); break;
        case json::Type::Number: {
            // The parser guarantees this is finite, but it can still be far outside int range,
            // so it is read through the checked accessor exactly as the plugin reads it.
            int asInt = 0;
            if (v.asInt(asInt)) acc += asInt;
            break;
        }
        case json::Type::Bool:   acc += v.boolean ? 1 : 0; break;
        case json::Type::Array:
            for (const json::Value& item : v.items) acc += walk(item, depth + 1);
            break;
        case json::Type::Object:
            for (const auto& kv : v.members) {
                acc += (long long)kv.first.size();
                acc += walk(kv.second, depth + 1);
            }
            // The same lookups the real code performs, including ones that will miss.
            acc += (long long)v.str("url").size();
            acc += (long long)v.str("webSocketDebuggerUrl").size();
            acc += (long long)v.str("message").size();
            acc += (long long)v.num("executionContextId", -1.0);
            acc += v.flag("value", false) ? 1 : 0;
            acc += v.member("frameTree") ? 1 : 0;
            acc += v.member("childFrames") ? 1 : 0;
            acc += v.member("nonexistent-key") ? 1 : 0;
            break;
        default: break;
    }
    return acc;
}

std::mt19937 rng(0xC0FFEE);

size_t pick(size_t n) { return n ? (size_t)(rng() % n) : 0; }

void mutate(std::string& s)
{
    if (s.empty()) { s.push_back((char)(rng() & 0xFF)); return; }

    switch (rng() % 8) {
        case 0: s[pick(s.size())] = (char)(rng() & 0xFF); break;                 // flip a byte
        case 1: s.insert(pick(s.size() + 1), 1, (char)(rng() & 0xFF)); break;    // insert
        case 2: s.erase(pick(s.size()), 1); break;                               // delete
        case 3: {                                                                // duplicate a run
            size_t a = pick(s.size());
            size_t n = 1 + pick(s.size() - a);
            if (s.size() + n < (1u << 20)) s.insert(a, s.substr(a, n));
            break;
        }
        case 4: s.resize(pick(s.size() + 1)); break;                             // truncate
        case 5: {                                                                // structural bytes
            static const char kInteresting[] = "{}[]\",:\\/ \t\r\n0123456789.eE-+utfalsnr";
            s[pick(s.size())] = kInteresting[pick(sizeof(kInteresting) - 1)];
            break;
        }
        case 6: s += kSeeds[pick(kSeedCount)]; break;                            // splice a seed
        case 7: {                                                                // deepen nesting
            size_t n = 1 + pick(64);
            s.insert(0, std::string(n, (rng() & 1) ? '[' : '{'));
            break;
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    long iterations = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 300000;

    long parsed = 0, refused = 0;
    long long sink = 0;

    for (long i = 0; i < iterations; ++i) {
        std::string input;

        if (i % 4 == 0) {
            // Pure random bytes, including embedded NULs.
            size_t n = pick(256);
            input.resize(n);
            for (size_t k = 0; k < n; ++k) input[k] = (char)(rng() & 0xFF);
        } else {
            input = kSeeds[pick(kSeedCount)];
            int rounds = 1 + (int)pick(6);
            for (int r = 0; r < rounds; ++r) mutate(input);
        }

        json::Value value;
        std::string err;
        if (json::parse(input, value, err)) {
            ++parsed;
            sink += walk(value, 0);
        } else {
            ++refused;
            // A refusal must always say why; an empty reason would be a path that forgot to.
            if (err.empty()) {
                printf("FAIL: refused with no reason, input size %zu\n", input.size());
                return 1;
            }
        }
    }

    printf("%ld inputs: %ld parsed, %ld refused (sink %lld)\n", iterations, parsed, refused, sink);
    printf("no crash or sanitizer error\n");
    return 0;
}
