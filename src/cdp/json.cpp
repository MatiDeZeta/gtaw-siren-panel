#include "cdp/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

namespace {

const int    kMaxDepth  = 64;
const size_t kMaxLength = 16u * 1024u * 1024u;

struct Parser
{
    const std::string& s;
    size_t             i = 0;
    int                depth = 0;
    std::string        err;

    explicit Parser(const std::string& text) : s(text) {}

    bool fail(const char* what)
    {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s at offset %zu", what, i);
        err = buf;
        return false;
    }

    void skipWs()
    {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++i;
            else break;
        }
    }

    bool literal(const char* word, size_t len)
    {
        if (i + len > s.size()) return false;
        if (std::memcmp(s.data() + i, word, len) != 0) return false;
        i += len;
        return true;
    }

    // Appends the UTF-8 encoding of cp to out.
    static void appendUtf8(std::string& out, unsigned int cp)
    {
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(unsigned int& out)
    {
        if (i + 4 > s.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i + k];
            unsigned int d;
            if (c >= '0' && c <= '9')      d = static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned int>(c - 'A' + 10);
            else return false;
            out = (out << 4) | d;
        }
        i += 4;
        return true;
    }

    bool parseString(std::string& out)
    {
        if (i >= s.size() || s[i] != '"') return fail("expected a string");
        ++i;

        for (;;) {
            if (i >= s.size()) return fail("unterminated string");
            unsigned char c = (unsigned char)s[i];

            if (c == '"') { ++i; return true; }

            if (c == '\\') {
                ++i;
                if (i >= s.size()) return fail("unterminated escape");
                char e = s[i++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        unsigned int cp = 0;
                        if (!hex4(cp)) return fail("bad \\u escape");
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // High surrogate: a low surrogate must follow to form one code point.
                            if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                size_t save = i;
                                i += 2;
                                unsigned int lo = 0;
                                if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    i = save;
                                    cp = 0xFFFD;
                                }
                            } else {
                                cp = 0xFFFD;
                            }
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            cp = 0xFFFD;   // lone low surrogate
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return fail("unknown escape");
                }
                continue;
            }

            if (c < 0x20) return fail("raw control character in string");
            out.push_back(char(c));
            ++i;
        }
    }

    bool parseNumber(Value& v)
    {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
                                s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+'))
            ++i;
        if (i == start) return fail("expected a number");

        std::string text = s.substr(start, i - start);
        char* end = nullptr;
        double d = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || (end && *end != '\0')) return fail("malformed number");

        // Something like 1e309 is well formed but overflows to infinity. Refusing it here keeps
        // the invariant that every Number in the tree is finite, which is what makes the
        // conversions the rest of the plugin performs on these values safe.
        if (!std::isfinite(d)) return fail("number out of range");

        v.type = Type::Number;
        v.number = d;
        return true;
    }

    bool parseValue(Value& v)
    {
        if (depth >= kMaxDepth) return fail("nested too deep");
        skipWs();
        if (i >= s.size()) return fail("unexpected end of input");

        char c = s[i];
        if (c == '"') { v.type = Type::String; return parseString(v.string); }
        if (c == '{') return parseObject(v);
        if (c == '[') return parseArray(v);
        if (c == 't') { if (!literal("true", 4))  return fail("expected true");  v.type = Type::Bool; v.boolean = true;  return true; }
        if (c == 'f') { if (!literal("false", 5)) return fail("expected false"); v.type = Type::Bool; v.boolean = false; return true; }
        if (c == 'n') { if (!literal("null", 4))  return fail("expected null");  v.type = Type::Null; return true; }
        return parseNumber(v);
    }

    bool parseArray(Value& v)
    {
        ++i;                // consume '['
        ++depth;
        v.type = Type::Array;

        skipWs();
        if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }

        for (;;) {
            Value item;
            if (!parseValue(item)) return false;
            v.items.push_back(std::move(item));

            skipWs();
            if (i >= s.size()) return fail("unterminated array");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; --depth; return true; }
            return fail("expected , or ] in array");
        }
    }

    bool parseObject(Value& v)
    {
        ++i;                // consume '{'
        ++depth;
        v.type = Type::Object;

        skipWs();
        if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }

        for (;;) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;

            skipWs();
            if (i >= s.size() || s[i] != ':') return fail("expected : after key");
            ++i;

            Value item;
            if (!parseValue(item)) return false;
            v.members.emplace_back(std::move(key), std::move(item));

            skipWs();
            if (i >= s.size()) return fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; --depth; return true; }
            return fail("expected , or } in object");
        }
    }
};

}  // namespace

const Value* Value::member(const std::string& key) const
{
    if (type != Type::Object) return nullptr;
    for (const auto& kv : members)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string Value::str(const std::string& key, const std::string& def) const
{
    const Value* v = member(key);
    return (v && v->isString()) ? v->string : def;
}

double Value::num(const std::string& key, double def) const
{
    const Value* v = member(key);
    return (v && v->isNumber()) ? v->number : def;
}

bool Value::flag(const std::string& key, bool def) const
{
    const Value* v = member(key);
    return (v && v->isBool()) ? v->boolean : def;
}

bool Value::asInt(int& out) const
{
    if (type != Type::Number || !std::isfinite(number)) return false;
    if (number < -2147483648.0 || number > 2147483647.0) return false;

    out = (int)number;
    return true;
}

bool parse(const std::string& text, Value& out, std::string& err)
{
    if (text.size() > kMaxLength) { err = "document too large"; return false; }

    Parser p(text);
    if (!p.parseValue(out)) { err = p.err; return false; }

    p.skipWs();
    if (p.i != text.size()) { err = "trailing data after value"; return false; }
    return true;
}

std::string quote(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\b': out += "\\b";  continue;
            case '\f': out += "\\f";  continue;
            case '\n': out += "\\n";  continue;
            case '\r': out += "\\r";  continue;
            case '\t': out += "\\t";  continue;
            default: break;
        }

        if (c < 0x20) {
            char buf[8];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", c);
            out += buf;
            continue;
        }

        // U+2028 LINE SEPARATOR and U+2029 PARAGRAPH SEPARATOR, as UTF-8 E2 80 A8 / E2 80 A9.
        if (c == 0xE2 && i + 2 < s.size() &&
            (unsigned char)s[i + 1] == 0x80 &&
            ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9)) {
            out += ((unsigned char)s[i + 2] == 0xA8) ? "\\u2028" : "\\u2029";
            i += 2;
            continue;
        }

        out.push_back(char(c));
    }

    out.push_back('"');
    return out;
}

}  // namespace json
