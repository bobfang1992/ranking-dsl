#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "plan/plan.h"
#include "plan/complexity.h"

namespace ranking_dsl {

class KeyRegistry;
class NodeRunner;

/**
 * Compiled plan ready for execution.
 */
struct CompiledPlan {
  Plan plan;
  std::vector<std::string> topo_order;  // Node IDs in execution order
  ComplexityMetrics complexity;         // Computed complexity metrics
  // Node runners are looked up at execution time
};

/**
 * Plan compiler - validates and prepares a plan for execution.
 */
class PlanCompiler {
 public:
  explicit PlanCompiler(const KeyRegistry& registry);

  /**
   * Set complexity budget for enforcement.
   * If not set, uses default budget.
   */
  void SetComplexityBudget(const ComplexityBudget& budget);

  /**
   * Disable complexity checking (for tests or special cases).
   */
  void DisableComplexityCheck();

  /**
   * Compile a plan.
   * Performs validation, complexity checking, and topological sorting.
   */
  bool Compile(const Plan& plan, CompiledPlan& out, std::string* error_out = nullptr);

 private:
  const KeyRegistry& registry_;
  std::optional<ComplexityBudget> budget_;
  bool complexity_check_enabled_ = true;

  bool ValidateNodeIds(const Plan& plan, std::string* error_out);
  bool TopologicalSort(const Plan& plan, std::vector<std::string>& out, std::string* error_out);
  bool ValidateOps(const Plan& plan, std::string* error_out);
  bool ValidatePlanEnv(const Plan& plan, std::string* error_out);
  bool ValidateComplexity(const Plan& plan, ComplexityMetrics& metrics, std::string* error_out);
};

}  // namespace ranking_dsl
