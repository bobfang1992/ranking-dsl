#include "object/value.h"

#include <fmt/format.h>

namespace ranking_dsl {

ValueType GetValueType(const Value& v) {
  return std::visit(
      [](auto&& arg) -> ValueType {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, NullValue>) {
          return ValueType::Null;
        } else if constexpr (std::is_same_v<T, bool>) {
          return ValueType::Bool;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return ValueType::I64;
        } else if constexpr (std::is_same_v<T, float>) {
          return ValueType::F32;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return ValueType::String;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
          return ValueType::Bytes;
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
          return ValueType::F32Vec;
        } else {
          static_assert(sizeof(T) == 0, "Unknown type in Value variant");
        }
      },
      v);
}

bool IsNull(const Value& v) {
  return std::holds_alternative<NullValue>(v);
}

std::string FormatValue(const Value& v) {
  return std::visit(
      [](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, NullValue>) {
          return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return fmt::format("{}", arg);
        } else if constexpr (std::is_same_v<T, float>) {
          return fmt::format("{:.6g}", arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return fmt::format("\"{}\"", arg);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
          return fmt::format("<bytes len={}>", arg.size());
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
          return fmt::format("<f32vec len={}>", arg.size());
        } else {
          static_assert(sizeof(T) == 0, "Unknown type in Value variant");
        }
      },
      v);
}

}  // namespace ranking_dsl
