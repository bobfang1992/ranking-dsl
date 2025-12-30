#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "expr/expr.h"

namespace ranking_dsl {

/**
 * Node specification in a plan.
 */
struct NodeSpec {
  std::string id;
  std::string op;
  std::vector<std::string> inputs;
  nlohmann::json params;
};

/**
 * Logging configuration.
 */
struct PlanLogging {
  float sample_rate = 0.0f;
  std::vector<int32_t> dump_keys;
};

/**
 * A ranking plan (DAG of nodes).
 */
struct Plan {
  std::string name;
  int version = 1;
  std::vector<NodeSpec> nodes;
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
