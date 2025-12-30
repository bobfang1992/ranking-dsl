#include "object/obj.h"

#include <stdexcept>

#include "keys/registry.h"

namespace ranking_dsl {

namespace {

// Check if a Value matches the expected KeyType
bool ValueMatchesType(const Value& v, keys::KeyType expected) {
  auto actual = GetValueType(v);
  switch (expected) {
    case keys::KeyType::Bool:
      return actual == ValueType::Bool || actual == ValueType::Null;
    case keys::KeyType::I64:
      return actual == ValueType::I64 || actual == ValueType::Null;
    case keys::KeyType::F32:
      return actual == ValueType::F32 || actual == ValueType::Null;
    case keys::KeyType::String:
      return actual == ValueType::String || actual == ValueType::Null;
    case keys::KeyType::Bytes:
      return actual == ValueType::Bytes || actual == ValueType::Null;
    case keys::KeyType::F32Vec:
      return actual == ValueType::F32Vec || actual == ValueType::Null;
  }
  return false;
}

}  // namespace

Obj::Obj(DataMap data) : data_(std::move(data)) {}

std::optional<Value> Obj::Get(int32_t key_id) const {
  auto it = data_.find(key_id);
  if (it == data_.end()) {
    return std::nullopt;
  }
  return it->second;
}

Obj Obj::Set(int32_t key_id, Value value, const KeyRegistry* registry) const {
  // Validate type if registry is provided
  if (registry) {
    const auto* key_info = registry->GetById(key_id);
    if (!key_info) {
      throw std::runtime_error("Unknown key ID: " + std::to_string(key_id));
    }
    if (!ValueMatchesType(value, key_info->type)) {
      throw std::runtime_error(
          "Type mismatch for key " + key_info->name +
          ": expected " + std::string(KeyTypeToString(key_info->type)) +
          ", got " + FormatValue(value));
    }
  }

  // Create new Obj with the updated value
  DataMap new_data = data_;  // Copy
  new_data[key_id] = std::move(value);
  return Obj(std::move(new_data));
}

bool Obj::Has(int32_t key_id) const {
  return data_.find(key_id) != data_.end();
}

Obj Obj::Del(int32_t key_id) const {
  DataMap new_data = data_;  // Copy
  new_data.erase(key_id);
  return Obj(std::move(new_data));
}

std::vector<int32_t> Obj::Keys() const {
  std::vector<int32_t> result;
  result.reserve(data_.size());
  for (const auto& [key, _] : data_) {
    result.push_back(key);
  }
  return result;
}

}  // namespace ranking_dsl
