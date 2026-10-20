#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace box::ecs {

/** Small dependency-free JSON value type used by scene serialization and prefabs. */
class JsonValue final {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    JsonValue(bool value) : value_(value) {}
    JsonValue(int value) : value_(static_cast<double>(value)) {}
    JsonValue(std::uint32_t value) : value_(static_cast<double>(value)) {}
    JsonValue(std::int64_t value) : value_(static_cast<double>(value)) {}
    JsonValue(double value) : value_(value) {}
    JsonValue(const char* value) : value_(std::string(value == nullptr ? "" : value)) {}
    JsonValue(std::string value) : value_(std::move(value)) {}
    JsonValue(Array value) : value_(std::move(value)) {}
    JsonValue(Object value) : value_(std::move(value)) {}

    [[nodiscard]] static JsonValue array() { return Array{}; }
    [[nodiscard]] static JsonValue object() { return Object{}; }

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;

    [[nodiscard]] bool asBool(bool fallback = false) const noexcept;
    [[nodiscard]] double asNumber(double fallback = 0.0) const noexcept;
    [[nodiscard]] const std::string* asString() const noexcept;
    [[nodiscard]] std::string* asString() noexcept;
    [[nodiscard]] const Array* asArray() const noexcept;
    [[nodiscard]] Array* asArray() noexcept;
    [[nodiscard]] const Object* asObject() const noexcept;
    [[nodiscard]] Object* asObject() noexcept;

    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
    [[nodiscard]] JsonValue* find(std::string_view key) noexcept;
    JsonValue& operator[](std::string key);
    [[nodiscard]] const JsonValue& at(std::string_view key) const;
    void pushBack(JsonValue value);

    [[nodiscard]] std::string stringify(unsigned int indentSpaces = 0) const;

    struct ParseResult;
    [[nodiscard]] static ParseResult parse(std::string_view source);

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage value_ = nullptr;
};

struct JsonValue::ParseResult {
    JsonValue value;
    std::string error;
    std::size_t errorOffset = 0;

    [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

}  // namespace box::ecs
