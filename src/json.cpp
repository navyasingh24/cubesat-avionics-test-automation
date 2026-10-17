#include "cubesat/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace cubesat::json {

namespace {

const char* typeName(Value::Type type) {
    switch (type) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return "bool";
        case Value::Type::Number: return "number";
        case Value::Type::String: return "string";
        case Value::Type::Array: return "array";
        case Value::Type::Object: return "object";
    }
    return "unknown";
}

[[noreturn]] void typeMismatch(Value::Type expected, Value::Type actual) {
    throw std::logic_error(std::string("JSON type mismatch: expected ") + typeName(expected) +
                           ", got " + typeName(actual));
}

void appendUtf8(std::string& out, unsigned long codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// Recursive-descent parser over a string_view. All errors carry the byte
// offset so a corrupt configuration file can be diagnosed quickly.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Value> parseDocument() {
        skipWhitespace();
        Value root;
        if (!parseValue(root, 0)) {
            return Result<Value>::fail("JSON_PARSE_ERROR", error_);
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            return Result<Value>::fail("JSON_PARSE_ERROR",
                                       "unexpected trailing characters at offset " +
                                           std::to_string(pos_));
        }
        return Result<Value>::ok(std::move(root));
    }

private:
    bool fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consumeLiteral(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    bool parseValue(Value& out, std::size_t depth) {
        if (depth > kMaxNestingDepth) {
            return fail("maximum nesting depth exceeded");
        }
        if (atEnd()) {
            return fail("unexpected end of input");
        }
        const char c = peek();
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Value(std::move(s));
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        if (consumeLiteral("true")) { out = Value(true); return true; }
        if (consumeLiteral("false")) { out = Value(false); return true; }
        if (consumeLiteral("null")) { out = Value(nullptr); return true; }
        return fail("unexpected character");
    }

    bool parseObject(Value& out, std::size_t depth) {
        ++pos_;  // '{'
        Value::Object members;
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            out = Value(std::move(members));
            return true;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') return fail("expected string key");
            std::string key;
            if (!parseString(key)) return false;
            for (const auto& member : members) {
                if (member.first == key) return fail("duplicate key '" + key + "'");
            }
            skipWhitespace();
            if (peek() != ':') return fail("expected ':'");
            ++pos_;
            skipWhitespace();
            Value value;
            if (!parseValue(value, depth + 1)) return false;
            members.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            return fail("expected ',' or '}'");
        }
        out = Value(std::move(members));
        return true;
    }

    bool parseArray(Value& out, std::size_t depth) {
        ++pos_;  // '['
        Value::Array items;
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            out = Value(std::move(items));
            return true;
        }
        while (true) {
            skipWhitespace();
            Value item;
            if (!parseValue(item, depth + 1)) return false;
            items.push_back(std::move(item));
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            return fail("expected ',' or ']'");
        }
        out = Value(std::move(items));
        return true;
    }

    bool parseHex4(unsigned long& out) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned long>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned long>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned long>(c - 'A' + 10);
            else return fail("invalid hex digit in \\u escape");
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        while (true) {
            if (atEnd()) return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (atEnd()) return fail("unterminated escape");
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned long cp = 0;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (!consumeLiteral("\\u")) return fail("unpaired high surrogate");
                        unsigned long low = 0;
                        if (!parseHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("unpaired low surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape sequence");
            }
        }
    }

    bool parseNumber(Value& out) {
        // Validate the RFC 8259 grammar first, then convert with strtod on a
        // bounded copy (strtod alone would accept hex, "inf", "nan", ...).
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            return fail("invalid number");
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) return fail("expected digit after '.'");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) return fail("expected digit in exponent");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string token(text_.substr(start, pos_ - start));
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(number)) {
            return fail("number out of range");
        }
        out = Value(number);
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

}  // namespace

bool Value::asBool() const {
    if (!isBool()) typeMismatch(Type::Bool, type());
    return std::get<bool>(data_);
}

double Value::asNumber() const {
    if (!isNumber()) typeMismatch(Type::Number, type());
    return std::get<double>(data_);
}

const std::string& Value::asString() const {
    if (!isString()) typeMismatch(Type::String, type());
    return std::get<std::string>(data_);
}

const Value::Array& Value::asArray() const {
    if (!isArray()) typeMismatch(Type::Array, type());
    return std::get<Array>(data_);
}

const Value::Object& Value::asObject() const {
    if (!isObject()) typeMismatch(Type::Object, type());
    return std::get<Object>(data_);
}

const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& member : std::get<Object>(data_)) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Value& Value::set(std::string key, Value value) {
    if (!isObject()) typeMismatch(Type::Object, type());
    auto& members = std::get<Object>(data_);
    for (auto& member : members) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    members.emplace_back(std::move(key), std::move(value));
    return *this;
}

void Value::push(Value value) {
    if (!isArray()) typeMismatch(Type::Array, type());
    std::get<Array>(data_).push_back(std::move(value));
}

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

void Value::dumpTo(std::string& out) const {
    switch (type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += asBool() ? "true" : "false"; break;
        case Type::Number: {
            const double number = asNumber();
            if (!std::isfinite(number)) {
                out += "null";  // JSON has no NaN/Infinity
                break;
            }
            // %.15g gives the shortest stable representation for the values
            // used here (e.g. 7.4 -> "7.4", 10 -> "10") and is locale-safe
            // because the program never calls setlocale().
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.15g", number);
            out += buffer;
            break;
        }
        case Type::String:
            out.push_back('"');
            out += escape(asString());
            out.push_back('"');
            break;
        case Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : asArray()) {
                if (!first) out.push_back(',');
                first = false;
                item.dumpTo(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& member : asObject()) {
                if (!first) out.push_back(',');
                first = false;
                out.push_back('"');
                out += escape(member.first);
                out += "\":";
                member.second.dumpTo(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

Result<Value> parse(std::string_view text) {
    return Parser(text).parseDocument();
}

}  // namespace cubesat::json
