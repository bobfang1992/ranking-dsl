#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "keys.h"  // Generated header

namespace ranking_dsl {

/**
 * Runtime key registry.
 *
 * Loads key definitions from keys.json and provides lookup functionality.
 * Used for runtime type validation.
 */
class KeyRegistry {
 public:
  /**
   * Key metadata.
   */
  struct KeyInfo {
    int32_t id;
    std::string name;
    keys::KeyType type;
    std::string scope;
    std::string owner;
    std::string doc;
  };

  /**
   * Create an empty registry.
   */
  KeyRegistry() = default;

  /**
   * Load registry from JSON file.
   * Returns false and sets error_out on failure.
   */
  bool LoadFromFile(const std::string& path, std::string* error_out = nullptr);

  /**
   * Load registry from JSON string.
   * Returns false and sets error_out on failure.
   */
  bool LoadFromJson(const std::string& json_str, std::string* error_out = nullptr);

  /**
   * Load registry from the compiled-in kAllKeys array.
   * Useful when keys.json is not available.
   */
  void LoadFromCompiled();

  /**
   * Look up a key by ID.
   */
  const KeyInfo* GetById(int32_t id) const;

  /**
   * Look up a key by name.
   */
  const KeyInfo* GetByName(std::string_view name) const;

  /**
   * Get all registered keys.
   */
  const std::vector<KeyInfo>& AllKeys() const { return keys_; }

  /**
   * Get registry version.
   */
  int Version() const { return version_; }

 private:
  int version_ = 0;
  std::vector<KeyInfo> keys_;
  std::unordered_map<int32_t, size_t> by_id_;
  std::unordered_map<std::string, size_t> by_name_;
};

/**
 * Map KeyType enum to string.
 */
std::string_view KeyTypeToString(keys::KeyType type);

/**
 * Parse KeyType from string.
 */
std::optional<keys::KeyType> ParseKeyType(std::string_view s);

}  // namespace ranking_dsl
