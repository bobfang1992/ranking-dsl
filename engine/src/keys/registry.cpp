#include "keys/registry.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ranking_dsl {

std::string_view KeyTypeToString(keys::KeyType type) {
  switch (type) {
    case keys::KeyType::Bool:
      return "bool";
    case keys::KeyType::I64:
      return "i64";
    case keys::KeyType::F32:
      return "f32";
    case keys::KeyType::String:
      return "string";
    case keys::KeyType::Bytes:
      return "bytes";
    case keys::KeyType::F32Vec:
      return "f32vec";
  }
  return "unknown";
}

std::optional<keys::KeyType> ParseKeyType(std::string_view s) {
  if (s == "bool") return keys::KeyType::Bool;
  if (s == "i64") return keys::KeyType::I64;
  if (s == "f32") return keys::KeyType::F32;
  if (s == "string") return keys::KeyType::String;
  if (s == "bytes") return keys::KeyType::Bytes;
  if (s == "f32vec") return keys::KeyType::F32Vec;
  return std::nullopt;
}

bool KeyRegistry::LoadFromFile(const std::string& path, std::string* error_out) {
  std::ifstream file(path);
  if (!file) {
    if (error_out) {
      *error_out = "Failed to open file: " + path;
    }
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  return LoadFromJson(content, error_out);
}

bool KeyRegistry::LoadFromJson(const std::string& json_str, std::string* error_out) {
  try {
    auto json = nlohmann::json::parse(json_str);

    version_ = json.value("version", 0);

    keys_.clear();
    by_id_.clear();
    by_name_.clear();

    for (const auto& key_json : json["keys"]) {
      KeyInfo info;
      info.id = key_json["id"].get<int32_t>();
      info.name = key_json["name"].get<std::string>();

      auto type_str = key_json["type"].get<std::string>();
      auto type_opt = ParseKeyType(type_str);
      if (!type_opt) {
        if (error_out) {
          *error_out = "Unknown key type: " + type_str;
        }
        return false;
      }
      info.type = *type_opt;

      info.scope = key_json["scope"].get<std::string>();
      info.owner = key_json["owner"].get<std::string>();
      info.doc = key_json["doc"].get<std::string>();

      size_t index = keys_.size();
      keys_.push_back(std::move(info));
      by_id_[keys_.back().id] = index;
      by_name_[keys_.back().name] = index;
    }

    return true;
  } catch (const std::exception& e) {
    if (error_out) {
      *error_out = std::string("JSON parse error: ") + e.what();
    }
    return false;
  }
}

void KeyRegistry::LoadFromCompiled() {
  keys_.clear();
  by_id_.clear();
  by_name_.clear();

  for (const auto& def : keys::kAllKeys) {
    KeyInfo info;
    info.id = def.id;
    info.name = std::string(def.name);
    info.type = def.type;
    // scope/owner/doc not available in compiled header
    info.scope = "";
    info.owner = "";
    info.doc = "";

    size_t index = keys_.size();
    keys_.push_back(std::move(info));
    by_id_[keys_.back().id] = index;
    by_name_[keys_.back().name] = index;
  }

  version_ = 1;
}

const KeyRegistry::KeyInfo* KeyRegistry::GetById(int32_t id) const {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) {
    return nullptr;
  }
  return &keys_[it->second];
}

const KeyRegistry::KeyInfo* KeyRegistry::GetByName(std::string_view name) const {
  auto it = by_name_.find(std::string(name));
  if (it == by_name_.end()) {
    return nullptr;
  }
  return &keys_[it->second];
}

}  // namespace ranking_dsl
