#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ranking_dsl {

/**
 * Null value type.
 */
struct NullValue {
  bool operator==(const NullValue&) const = default;
};

/**
 * Value variant - represents all possible runtime value types.
 *
 * Supported types:
 * - null
 * - bool
 * - i64 (int64_t)
 * - f32 (float)
 * - string
 * - bytes (vector<uint8_t>)
 * - f32vec (vector<float>)
 */
using Value = std::variant<
    NullValue,
    bool,
    int64_t,
    float,
    std::string,
    std::vector<uint8_t>,
    std::vector<float>>;

/**
 * Value type enumeration (matches KeyType).
 */
enum class ValueType : uint8_t {
  Null = 0,
  Bool = 1,
  I64 = 2,
  F32 = 3,
  String = 4,
  Bytes = 5,
  F32Vec = 6,
};

/**
 * Get the type of a Value.
 */
ValueType GetValueType(const Value& v);

/**
 * Check if a Value is null.
 */
bool IsNull(const Value& v);

/**
 * Create a null Value.
 */
inline Value MakeNull() { return NullValue{}; }

/**
 * Format a Value to string (for debugging/logging).
 */
std::string FormatValue(const Value& v);

}  // namespace ranking_dsl
