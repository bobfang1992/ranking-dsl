#include "plan/plan.h"

#include <fstream>
#include <regex>

#include <nlohmann/json.hpp>

namespace ranking_dsl {

// Validate trace_key: 1-64 chars, charset [A-Za-z0-9._/-]
std::string ValidateTraceKey(const std::string& trace_key) {
  if (trace_key.empty()) {
    return "";  // Empty is valid (means not set)
  }
  if (trace_key.length() > 64) {
    return "trace_key must be at most 64 characters (got " +
           std::to_string(trace_key.length()) + ")";
  }
  static const std::regex pattern("^[A-Za-z0-9._/-]+$");
  if (!std::regex_match(trace_key, pattern)) {
    return "trace_key must only contain [A-Za-z0-9._/-]";
  }
  return "";
}

bool ParsePlan(const nlohmann::json& json, Plan& out, std::string* error_out) {
  try {
    out.name = json.value("name", "unnamed");
    out.version = json.value("version", 1);

    out.nodes.clear();
    for (const auto& node_json : json["nodes"]) {
      NodeSpec node;
      node.id = node_json["id"].get<std::string>();
      node.op = node_json["op"].get<std::string>();

      if (node_json.contains("inputs")) {
        for (const auto& input : node_json["inputs"]) {
          node.inputs.push_back(input.get<std::string>());
        }
      }

      node.params = node_json.value("params", nlohmann::json::object());

      // Parse trace_key (optional, NOT inside params)
      if (node_json.contains("trace_key")) {
        node.trace_key = node_json["trace_key"].get<std::string>();
        std::string validation_error = ValidateTraceKey(node.trace_key);
        if (!validation_error.empty()) {
          if (error_out) {
            *error_out = "Node '" + node.id + "': " + validation_error;
          }
          return false;
        }
      }

      out.nodes.push_back(std::move(node));
    }

    if (json.contains("logging")) {
      const auto& log_json = json["logging"];
      out.logging.sample_rate = log_json.value("sample_rate", 0.0f);
      if (log_json.contains("dump_keys")) {
        for (const auto& key : log_json["dump_keys"]) {
          out.logging.dump_keys.push_back(key.get<int32_t>());
        }
      }
    }

    return true;
  } catch (const std::exception& e) {
    if (error_out) {
      *error_out = std::string("Plan parse error: ") + e.what();
    }
    return false;
  }
}

bool ParsePlanFile(const std::string& path, Plan& out, std::string* error_out) {
  std::ifstream file(path);
  if (!file) {
    if (error_out) {
      *error_out = "Failed to open file: " + path;
    }
    return false;
  }

  try {
    nlohmann::json json = nlohmann::json::parse(file);
    return ParsePlan(json, out, error_out);
  } catch (const std::exception& e) {
    if (error_out) {
      *error_out = std::string("JSON parse error: ") + e.what();
    }
    return false;
  }
}

}  // namespace ranking_dsl
