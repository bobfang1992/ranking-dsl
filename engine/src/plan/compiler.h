#pragma once

#include <memory>
#include <string>
#include <vector>

#include "plan/plan.h"

namespace ranking_dsl {

class KeyRegistry;
class NodeRunner;

/**
 * Compiled plan ready for execution.
 */
struct CompiledPlan {
  Plan plan;
  std::vector<std::string> topo_order;  // Node IDs in execution order
  // Node runners are looked up at execution time
};

/**
 * Plan compiler - validates and prepares a plan for execution.
 */
class PlanCompiler {
 public:
  explicit PlanCompiler(const KeyRegistry& registry);

  /**
   * Compile a plan.
   * Performs validation and topological sorting.
   */
  bool Compile(const Plan& plan, CompiledPlan& out, std::string* error_out = nullptr);

 private:
  const KeyRegistry& registry_;

  bool ValidateNodeIds(const Plan& plan, std::string* error_out);
  bool TopologicalSort(const Plan& plan, std::vector<std::string>& out, std::string* error_out);
  bool ValidateOps(const Plan& plan, std::string* error_out);
};

}  // namespace ranking_dsl
