#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "expr/expr.h"

namespace ranking_dsl {

/**
 * Node instance in a plan (renamed from NodeSpec to avoid conflict with node metadata).
 */
struct PlanNode {
  std::string id;
  std::string op;
  std::vector<std::string> inputs;
  nlohmann::json params;
  std::string trace_key;  // Optional trace key for tracing/logging (empty = not set)
};

/**
 * Validate a trace_key value.
 * Returns empty string if valid, error message if invalid.
 */
std::string ValidateTraceKey(const std::string& trace_key);

/**
 * Logging configuration.
 */
struct PlanLogging {
  float sample_rate = 0.0f;
  std::vector<int32_t> dump_keys;
};

/**
 * Plan metadata.
 */
struct PlanMeta {
  std::string env = "dev";  // Plan environment: "prod", "dev", or "test"
};

/**
 * A ranking plan (DAG of nodes).
 */
struct Plan {
  std::string name;
  int version = 1;
  PlanMeta meta;
  std::vector<PlanNode> nodes;
  PlanLogging logging;
};

/**
 * Parse a Plan from JSON.
 */
bool ParsePlan(const nlohmann::json& json, Plan& out, std::string* error_out = nullptr);

/**
 * Parse a Plan from a JSON file.
 */
bool ParsePlanFile(const std::string& path, Plan& out, std::string* error_out = nullptr);

}  // namespace ranking_dsl
