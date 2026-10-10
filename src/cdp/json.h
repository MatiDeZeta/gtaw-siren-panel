#pragma once

#include <string>
#include <utility>
#include <vector>

// A small JSON reader and string encoder, written here rather than pulled in, so the plugin has
// no third-party code in it at all. It only has to handle what the debugging protocol sends
// back, but it is still the component that touches untrusted bytes off a socket, so it caps
// nesting depth and total length instead of trusting the input to be well behaved.
namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value
{
    Type   type    = Type::Null;
    bool   boolean = false;
    double number  = 0.0;
    std::string string;
    std::vector<Value> items;                                 // Array
    std::vector<std::pair<std::string, Value>> members;       // Object, in document order

    bool isNull()   const { return type == Type::Null; }
    bool isBool()   const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray()  const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // Null for a missing key, or when this value is not an object.
    const Value* member(const std::string& key) const;

    std::string str(const std::string& key, const std::string& def = std::string()) const;
    double      num(const std::string& key, double def = 0.0) const;
    bool        flag(const std::string& key, bool def = false) const;

    // True only when this is a number that fits exactly in an int. Every protocol value that
    // becomes an id goes through this: converting an out-of-range double to int is undefined
    // behaviour, and these numbers come off a socket.
    bool asInt(int& out) const;
};

// Returns false and fills err on malformed input, input that nests deeper than the limit, or
// input longer than the limit. out is left in an unspecified state on failure.
bool parse(const std::string& text, Value& out, std::string& err);

// Wraps s in quotes and escapes it for use in a JSON document. U+2028 and U+2029 are escaped
// as well: the result is pasted into a JavaScript expression, where those two are not plain
// string characters in every parser.
std::string quote(const std::string& s);

}  // namespace json
