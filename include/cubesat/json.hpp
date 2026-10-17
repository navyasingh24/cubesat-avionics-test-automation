// Minimal, strict JSON value type, parser and serializer.
//
// Written in-house so the simulator has no third-party dependencies and can
// be built fully offline. It supports exactly what the project needs:
//   * RFC 8259 syntax (objects, arrays, strings, numbers, true/false/null)
//   * \uXXXX escapes including surrogate pairs (encoded as UTF-8)
//   * a nesting-depth limit to avoid stack exhaustion on hostile input
//   * rejection of duplicate object keys (ambiguous configuration)
// Objects keep insertion order so serialized output is deterministic.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "cubesat/result.hpp"

namespace cubesat::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool b) : data_(b) {}
    template <typename T,
              std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T number) : data_(static_cast<double>(number)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    static Value object() { return Value(Object{}); }
    static Value array() { return Value(Array{}); }

    Type type() const { return static_cast<Type>(data_.index()); }
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isNumber() const { return type() == Type::Number; }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    // Accessors throw std::logic_error on a type mismatch; callers are
    // expected to check the type first (programming error otherwise).
    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Object helpers. find() returns nullptr when the key is absent or this
    // value is not an object.
    const Value* find(std::string_view key) const;
    Value& set(std::string key, Value value);  // insert or replace
    void push(Value value);                    // array append

    std::string dump() const;  // compact, single-line serialization

private:
    void dumpTo(std::string& out) const;

    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

constexpr std::size_t kMaxNestingDepth = 64;

// Parses a complete JSON document. Trailing non-whitespace is an error.
Result<Value> parse(std::string_view text);

// Escapes a string for inclusion in JSON output (without surrounding quotes).
std::string escape(std::string_view text);

}  // namespace cubesat::json
