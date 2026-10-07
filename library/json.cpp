#include "json.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
namespace iq::json {
const Value* Value::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (std::size_t k = 0; k < keys.size(); ++k)
        if (keys[k] == key) return &members[k];
    return nullptr;
}
namespace {
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
    Value document() {
        Value value = parse_value(0);
        skip_space();
        if (pos_ != text_.size()) fail("unexpected trailing characters");
        return value;
    }
private:
    [[noreturn]] void fail(const char* what) const {
        throw std::invalid_argument(std::string("Invalid JSON at byte ") + std::to_string(pos_) + ": " + what);
    }
    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) ++pos_;
    }
    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        ++pos_;
    }
    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return false;
        pos_ += word.size();
        return true;
    }
    Value parse_value(int depth) {
        if (depth > 64) fail("nesting too deep");
        skip_space();
        Value value;
        const char c = peek();
        if (c == '{') parse_object(value, depth);
        else if (c == '[') parse_array(value, depth);
        else if (c == '"') { value.type = Value::Type::String; value.string = parse_string(); }
        else if (literal("true")) { value.type = Value::Type::Bool; value.boolean = true; }
        else if (literal("false")) { value.type = Value::Type::Bool; }
        else if (literal("null")) {}
        else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) { value.type = Value::Type::Number; value.number = parse_number(); }
        else fail("unexpected character");
        return value;
    }
    void parse_object(Value& value, int depth) {
        value.type = Value::Type::Object;
        expect('{');
        skip_space();
        if (peek() == '}') { ++pos_; return; }
        for (;;) {
            skip_space();
            if (peek() != '"') fail("expected a member name");
            value.keys.push_back(parse_string());
            skip_space();
            expect(':');
            value.members.push_back(parse_value(depth + 1));
            skip_space();
            if (peek() == ',') { ++pos_; continue; }
            expect('}');
            return;
        }
    }
    void parse_array(Value& value, int depth) {
        value.type = Value::Type::Array;
        expect('[');
        skip_space();
        if (peek() == ']') { ++pos_; return; }
        for (;;) {
            value.array.push_back(parse_value(depth + 1));
            skip_space();
            if (peek() == ',') { ++pos_; continue; }
            expect(']');
            return;
        }
    }
    double parse_number() {
        const auto begin = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                                       text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
        double number = 0;
        const auto result = std::from_chars(text_.data() + begin, text_.data() + pos_, number);
        if (result.ec != std::errc() || result.ptr != text_.data() + pos_ || !std::isfinite(number)) { pos_ = begin; fail("bad number"); }
        return number;
    }
    unsigned hex4() {
        if (pos_ + 4 > text_.size()) fail("short unicode escape");
        unsigned code = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else fail("bad unicode escape");
        }
        return code;
    }
    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
        else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    std::string parse_string() {
        expect('"');
        std::string out;
        for (;;) {
            if (pos_ >= text_.size()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (pos_ >= text_.size()) fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e) {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = hex4();
                if (code >= 0xD800 && code < 0xDC00 && text_.substr(pos_, 2) == "\\u") {
                    pos_ += 2;
                    const unsigned low = hex4();
                    if (low < 0xDC00 || low > 0xDFFF) fail("bad surrogate pair");
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, code);
                break;
            }
            default: fail("bad escape");
            }
        }
    }
    std::string_view text_;
    std::size_t pos_ = 0;
};
}
Value parse(std::string_view text) { return Parser(text).document(); }
}
