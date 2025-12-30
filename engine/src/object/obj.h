#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include "object/value.h"

namespace ranking_dsl {

class KeyRegistry;  // Forward declaration

/**
 * Obj - An immutable key-value map representing one candidate object.
 *
 * All keys are identified by their integer key_id (from the registry).
 * Set operations return a new Obj; the original is unchanged.
 *
 * MVP implementation uses copying maps. Structural sharing can be
 * added later for performance optimization.
 */
class Obj {
 public:
  using DataMap = std::unordered_map<int32_t, Value>;

  /**
   * Create an empty Obj.
   */
  Obj() = default;

  /**
   * Create an Obj from existing data.
   */
  explicit Obj(DataMap data);

  /**
   * Get a value by key_id.
   * Returns std::nullopt if the key is not present.
   */
  std::optional<Value> Get(int32_t key_id) const;

  /**
   * Set a value, returning a new Obj.
   * The original Obj is unchanged.
   *
   * If registry is provided, validates that the value type matches
   * the key's declared type. Throws on type mismatch.
   */
  Obj Set(int32_t key_id, Value value, const KeyRegistry* registry = nullptr) const;

  /**
   * Check if a key is present.
   */
  bool Has(int32_t key_id) const;

  /**
   * Delete a key, returning a new Obj.
   * If the key doesn't exist, returns a copy of this Obj.
   */
  Obj Del(int32_t key_id) const;

  /**
   * Get the number of key-value pairs.
   */
  size_t Size() const { return data_.size(); }

  /**
   * Get all key IDs in this Obj.
   */
  std::vector<int32_t> Keys() const;

  /**
   * Get the underlying data (for iteration/inspection).
   */
  const DataMap& Data() const { return data_; }

 private:
  DataMap data_;
};

}  // namespace ranking_dsl
