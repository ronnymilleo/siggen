#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace iq::json {
// Minimal JSON document model, enough to read SigMF and export metadata files.
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::string> keys; // Object members, in document order (parallel to `members`).
    std::vector<Value> members;
    // Member of an object, or nullptr when absent or when this is not an object.
    const Value* find(std::string_view key) const;
    bool is_string() const { return type == Type::String; }
    bool is_number() const { return type == Type::Number; }
    bool is_object() const { return type == Type::Object; }
};
// Throws std::invalid_argument with the byte offset on malformed input or
// trailing characters. Nesting deeper than 64 levels is rejected.
Value parse(std::string_view text);
}
