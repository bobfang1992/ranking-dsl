/**
 * Standalone utility to export all registered core NodeSpecs as JSON.
 * This is used by the TypeScript CLI command `rankdsl nodes export`.
 */

#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>

#include "nodes/registry.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;
using json = nlohmann::json;

// Convert WritesDescriptor to JSON
json WritesDescriptorToJson(const WritesDescriptor& writes, const KeyRegistry& key_registry) {
  json j;

  if (writes.kind == WritesDescriptor::Kind::kStatic) {
    j["kind"] = "static";
    j["keys"] = json::array();
    for (int32_t key_id : writes.static_keys) {
      auto* key_info = key_registry.GetKeyInfo(key_id);
      json key_obj;
      key_obj["id"] = key_id;
      if (key_info) {
        key_obj["name"] = key_info->name;
      }
      j["keys"].push_back(key_obj);
    }
  } else {
    j["kind"] = "param_derived";
    j["param_name"] = writes.param_name;
  }

  return j;
}

// Convert NodeSpec to JSON
json NodeSpecToJson(const NodeSpec& spec, const KeyRegistry& key_registry) {
  json j;

  j["op"] = spec.op;
  j["namespace_path"] = spec.namespace_path;
  j["stability"] = (spec.stability == Stability::kStable) ? "stable" : "experimental";
  j["doc"] = spec.doc;
  j["kind"] = "core";  // All C++ nodes are "core"

  // Parse params_schema_json to ensure it's valid JSON
  if (!spec.params_schema_json.empty()) {
    try {
      j["params_schema"] = json::parse(spec.params_schema_json);
    } catch (const json::parse_error& e) {
      // If parsing fails, include as string
      j["params_schema_raw"] = spec.params_schema_json;
    }
  }

  // Reads
  j["reads"] = json::array();
  for (int32_t key_id : spec.reads) {
    auto* key_info = key_registry.GetKeyInfo(key_id);
    json key_obj;
    key_obj["id"] = key_id;
    if (key_info) {
      key_obj["name"] = key_info->name;
    }
    j["reads"].push_back(key_obj);
  }

  // Writes
  j["writes"] = WritesDescriptorToJson(spec.writes, key_registry);

  // Optional fields
  if (!spec.budgets_json.empty()) {
    try {
      j["budgets"] = json::parse(spec.budgets_json);
    } catch (const json::parse_error& e) {
      j["budgets_raw"] = spec.budgets_json;
    }
  }

  if (!spec.capabilities_json.empty()) {
    try {
      j["capabilities"] = json::parse(spec.capabilities_json);
    } catch (const json::parse_error& e) {
      j["capabilities_raw"] = spec.capabilities_json;
    }
  }

  return j;
}

int main(int argc, char* argv[]) {
  // Load key registry for key name lookups
  KeyRegistry key_registry;
  key_registry.LoadFromCompiled();

  // Get all registered NodeSpecs
  auto specs = NodeRegistry::Instance().GetAllSpecs();

  // Convert to JSON
  json output = json::array();
  for (const auto& spec : specs) {
    output.push_back(NodeSpecToJson(spec, key_registry));
  }

  // Print to stdout
  std::cout << output.dump(2) << std::endl;

  return 0;
}
