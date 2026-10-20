#include "box/ecs/json.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace box::ecs {
namespace {

void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7FU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

void stringifyString(std::ostringstream& output, const std::string& value) {
    output << '"';
    for (const char rawCharacter : value) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    output << '"';
}

void stringifyValue(std::ostringstream& output, const JsonValue& value,
                    unsigned int indentSpaces, unsigned int depth) {
    if (value.isNull()) {
        output << "null";
    } else if (value.isBool()) {
        output << (value.asBool() ? "true" : "false");
    } else if (value.isNumber()) {
        const double number = value.asNumber();
        if (!std::isfinite(number)) {
            output << "null";
        } else {
            output << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
        }
    } else if (const std::string* stringValue = value.asString()) {
        stringifyString(output, *stringValue);
    } else if (const JsonValue::Array* arrayValue = value.asArray()) {
        output << '[';
        for (std::size_t index = 0; index < arrayValue->size(); ++index) {
            if (index != 0U) output << ',';
            if (indentSpaces != 0U) {
                output << '\n' << std::string((depth + 1U) * indentSpaces, ' ');
            }
            stringifyValue(output, (*arrayValue)[index], indentSpaces, depth + 1U);
        }
        if (!arrayValue->empty() && indentSpaces != 0U) {
            output << '\n' << std::string(depth * indentSpaces, ' ');
        }
        output << ']';
    } else if (const JsonValue::Object* objectValue = value.asObject()) {
        output << '{';
        std::size_t index = 0;
        for (const auto& [key, child] : *objectValue) {
            if (index++ != 0U) output << ',';
            if (indentSpaces != 0U) {
                output << '\n' << std::string((depth + 1U) * indentSpaces, ' ');
            }
            stringifyString(output, key);
            output << (indentSpaces == 0U ? ":" : ": ");
            stringifyValue(output, child, indentSpaces, depth + 1U);
        }
        if (!objectValue->empty() && indentSpaces != 0U) {
            output << '\n' << std::string(depth * indentSpaces, ' ');
        }
        output << '}';
    }
}

class Parser final {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    [[nodiscard]] JsonValue::ParseResult parse() {
        JsonValue::ParseResult result{};
        skipWhitespace();
        if (!parseValue(result.value, 0U)) {
            result.error = error_;
            result.errorOffset = position_;
            return result;
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            setError("Unexpected trailing characters.");
            result.error = error_;
            result.errorOffset = position_;
        }
        return result;
    }

private:
    static constexpr std::size_t kMaximumDepth = 128U;

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\t' && character != '\n' && character != '\r') break;
            ++position_;
        }
    }

    void setError(const char* message) {
        if (error_.empty()) error_ = message;
    }

    [[nodiscard]] bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    [[nodiscard]] bool parseValue(JsonValue& value, std::size_t depth) {
        if (depth > kMaximumDepth) {
            setError("JSON nesting exceeds the supported limit.");
            return false;
        }
        skipWhitespace();
        if (position_ >= input_.size()) {
            setError("Unexpected end of JSON input.");
            return false;
        }

        const char character = input_[position_];
        if (character == '{') return parseObject(value, depth + 1U);
        if (character == '[') return parseArray(value, depth + 1U);
        if (character == '"') {
            std::string parsedString;
            if (!parseString(parsedString)) return false;
            value = JsonValue(std::move(parsedString));
            return true;
        }
        if (character == 't') return parseLiteral("true", JsonValue(true), value);
        if (character == 'f') return parseLiteral("false", JsonValue(false), value);
        if (character == 'n') return parseLiteral("null", JsonValue(nullptr), value);
        return parseNumber(value);
    }

    [[nodiscard]] bool parseLiteral(std::string_view literal, JsonValue parsed, JsonValue& result) {
        if (input_.substr(position_, literal.size()) != literal) {
            setError("Invalid JSON literal.");
            return false;
        }
        position_ += literal.size();
        result = std::move(parsed);
        return true;
    }

    [[nodiscard]] bool parseObject(JsonValue& value, std::size_t depth) {
        if (!consume('{')) {
            setError("Expected '{' to begin an object.");
            return false;
        }
        JsonValue::Object object;
        skipWhitespace();
        if (consume('}')) {
            value = JsonValue(std::move(object));
            return true;
        }
        while (position_ < input_.size()) {
            skipWhitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                setError("Expected a quoted object key.");
                return false;
            }
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) {
                setError("Expected ':' after an object key.");
                return false;
            }
            JsonValue child;
            if (!parseValue(child, depth)) return false;
            if (!object.emplace(std::move(key), std::move(child)).second) {
                setError("Duplicate JSON object key.");
                return false;
            }
            skipWhitespace();
            if (consume('}')) {
                value = JsonValue(std::move(object));
                return true;
            }
            if (!consume(',')) {
                setError("Expected ',' or '}' in an object.");
                return false;
            }
        }
        setError("Unterminated JSON object.");
        return false;
    }

    [[nodiscard]] bool parseArray(JsonValue& value, std::size_t depth) {
        if (!consume('[')) {
            setError("Expected '[' to begin an array.");
            return false;
        }
        JsonValue::Array array;
        skipWhitespace();
        if (consume(']')) {
            value = JsonValue(std::move(array));
            return true;
        }
        while (position_ < input_.size()) {
            JsonValue child;
            if (!parseValue(child, depth)) return false;
            array.push_back(std::move(child));
            skipWhitespace();
            if (consume(']')) {
                value = JsonValue(std::move(array));
                return true;
            }
            if (!consume(',')) {
                setError("Expected ',' or ']' in an array.");
                return false;
            }
        }
        setError("Unterminated JSON array.");
        return false;
    }

    [[nodiscard]] bool parseString(std::string& result) {
        if (!consume('"')) return false;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return true;
            if (character < 0x20U) {
                setError("Unescaped control character in JSON string.");
                return false;
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                setError("Unterminated JSON escape sequence.");
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codePoint = 0;
                    if (!parseHexCodeUnit(codePoint)) return false;
                    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                        if (position_ + 2U > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            setError("High surrogate is missing its low surrogate.");
                            return false;
                        }
                        position_ += 2U;
                        std::uint32_t lowSurrogate = 0;
                        if (!parseHexCodeUnit(lowSurrogate) || lowSurrogate < 0xDC00U ||
                            lowSurrogate > 0xDFFFU) {
                            setError("Invalid low surrogate in JSON string.");
                            return false;
                        }
                        codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) +
                                    (lowSurrogate - 0xDC00U);
                    } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
                        setError("Unexpected low surrogate in JSON string.");
                        return false;
                    }
                    appendUtf8(result, codePoint);
                    break;
                }
                default:
                    setError("Unknown JSON escape sequence.");
                    return false;
            }
        }
        setError("Unterminated JSON string.");
        return false;
    }

    [[nodiscard]] bool parseHexCodeUnit(std::uint32_t& value) {
        if (position_ + 4U > input_.size()) {
            setError("Incomplete Unicode escape sequence.");
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < 4U; ++index) {
            const char character = input_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9') {
                value |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                setError("Invalid hexadecimal digit in Unicode escape.");
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool parseNumber(JsonValue& value) {
        const std::size_t start = position_;
        (void)consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                setError("Leading zero in JSON number.");
                return false;
            }
        } else {
            if (!consumeDigits(true)) return false;
        }
        if (consume('.')) {
            if (!consumeDigits(true)) return false;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (!consumeDigits(true)) return false;
        }
        if (start == position_) {
            setError("Expected a JSON value.");
            return false;
        }

        const std::string token(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0' || !std::isfinite(number)) {
            setError("Invalid or non-finite JSON number.");
            return false;
        }
        value = JsonValue(number);
        return true;
    }

    [[nodiscard]] bool consumeDigits(bool requireAtLeastOne) {
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        if (requireAtLeastOne && start == position_) {
            setError("Expected a digit in JSON number.");
            return false;
        }
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::string error_;
};

}  // namespace

bool JsonValue::isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
bool JsonValue::isBool() const noexcept { return std::holds_alternative<bool>(value_); }
bool JsonValue::isNumber() const noexcept { return std::holds_alternative<double>(value_); }
bool JsonValue::isString() const noexcept { return std::holds_alternative<std::string>(value_); }
bool JsonValue::isArray() const noexcept { return std::holds_alternative<Array>(value_); }
bool JsonValue::isObject() const noexcept { return std::holds_alternative<Object>(value_); }

bool JsonValue::asBool(bool fallback) const noexcept {
    const bool* value = std::get_if<bool>(&value_);
    return value == nullptr ? fallback : *value;
}

double JsonValue::asNumber(double fallback) const noexcept {
    const double* value = std::get_if<double>(&value_);
    return value == nullptr ? fallback : *value;
}

const std::string* JsonValue::asString() const noexcept { return std::get_if<std::string>(&value_); }
std::string* JsonValue::asString() noexcept { return std::get_if<std::string>(&value_); }
const JsonValue::Array* JsonValue::asArray() const noexcept { return std::get_if<Array>(&value_); }
JsonValue::Array* JsonValue::asArray() noexcept { return std::get_if<Array>(&value_); }
const JsonValue::Object* JsonValue::asObject() const noexcept { return std::get_if<Object>(&value_); }
JsonValue::Object* JsonValue::asObject() noexcept { return std::get_if<Object>(&value_); }

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    const Object* objectValue = asObject();
    if (objectValue == nullptr) return nullptr;
    const auto found = objectValue->find(key);
    return found == objectValue->end() ? nullptr : &found->second;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
    Object* objectValue = asObject();
    if (objectValue == nullptr) return nullptr;
    const auto found = objectValue->find(key);
    return found == objectValue->end() ? nullptr : &found->second;
}

JsonValue& JsonValue::operator[](std::string key) {
    if (!isObject()) value_ = Object{};
    return std::get<Object>(value_)[std::move(key)];
}

const JsonValue& JsonValue::at(std::string_view key) const {
    const JsonValue* value = find(key);
    if (value == nullptr) throw std::out_of_range("JSON object key was not found.");
    return *value;
}

void JsonValue::pushBack(JsonValue value) {
    if (!isArray()) value_ = Array{};
    std::get<Array>(value_).push_back(std::move(value));
}

std::string JsonValue::stringify(unsigned int indentSpaces) const {
    std::ostringstream output;
    stringifyValue(output, *this, indentSpaces, 0U);
    return output.str();
}

JsonValue::ParseResult JsonValue::parse(std::string_view source) {
    return Parser(source).parse();
}

}  // namespace box::ecs
