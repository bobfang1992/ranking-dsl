#include "plan/compiler.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "keys/registry.h"
#include "nodes/registry.h"
#include "plan/complexity.h"

namespace ranking_dsl {

PlanCompiler::PlanCompiler(const KeyRegistry& registry) : registry_(registry) {}

void PlanCompiler::SetComplexityBudget(const ComplexityBudget& budget) {
  budget_ = budget;
}

void PlanCompiler::DisableComplexityCheck() {
  complexity_check_enabled_ = false;
}

bool PlanCompiler::Compile(const Plan& plan, CompiledPlan& out, std::string* error_out) {
  // Validate node IDs are unique
  if (!ValidateNodeIds(plan, error_out)) {
    return false;
  }

  // Topological sort
  std::vector<std::string> topo_order;
  if (!TopologicalSort(plan, topo_order, error_out)) {
    return false;
  }

  // Validate ops are known
  if (!ValidateOps(plan, error_out)) {
    return false;
  }

  // Validate experimental nodes not in prod
  if (!ValidatePlanEnv(plan, error_out)) {
    return false;
  }

  // Validate complexity budgets
  ComplexityMetrics metrics;
  if (!ValidateComplexity(plan, metrics, error_out)) {
    return false;
  }

  out.plan = plan;
  out.topo_order = std::move(topo_order);
  out.complexity = std::move(metrics);
  return true;
}

bool PlanCompiler::ValidateComplexity(const Plan& plan, ComplexityMetrics& metrics, std::string* error_out) {
  // Compute metrics (always, for reporting)
  metrics = ComputeComplexityMetrics(plan);

  // Skip enforcement if disabled
  if (!complexity_check_enabled_) {
    return true;
  }

  // Use provided budget or default
  ComplexityBudget budget = budget_.value_or(ComplexityBudget::Default());

  // Check against budget
  ComplexityCheckResult result = CheckComplexityBudget(metrics, budget);

  if (!result.passed) {
    if (error_out) {
      *error_out = result.diagnostics;
    }
    return false;
  }

  return true;
}

bool PlanCompiler::ValidateNodeIds(const Plan& plan, std::string* error_out) {
  std::unordered_set<std::string> seen;
  for (const auto& node : plan.nodes) {
    if (seen.count(node.id)) {
      if (error_out) {
        *error_out = "Duplicate node ID: " + node.id;
      }
      return false;
    }
    seen.insert(node.id);
  }
  return true;
}

bool PlanCompiler::TopologicalSort(const Plan& plan, std::vector<std::string>& out, std::string* error_out) {
  // Build adjacency and in-degree maps
  std::unordered_map<std::string, std::vector<std::string>> adj;  // node -> dependents
  std::unordered_map<std::string, int> in_degree;

  for (const auto& node : plan.nodes) {
    adj[node.id];  // Ensure entry exists
    in_degree[node.id] = static_cast<int>(node.inputs.size());

    for (const auto& input : node.inputs) {
      adj[input].push_back(node.id);
    }
  }

  // Kahn's algorithm
  std::queue<std::string> queue;
  for (const auto& node : plan.nodes) {
    if (in_degree[node.id] == 0) {
      queue.push(node.id);
    }
  }

  out.clear();
  while (!queue.empty()) {
    std::string current = queue.front();
    queue.pop();
    out.push_back(current);

    for (const auto& dependent : adj[current]) {
      in_degree[dependent]--;
      if (in_degree[dependent] == 0) {
        queue.push(dependent);
      }
    }
  }

  if (out.size() != plan.nodes.size()) {
    if (error_out) {
      *error_out = "Plan contains a cycle";
    }
    return false;
  }

  return true;
}

bool PlanCompiler::ValidateOps(const Plan& plan, std::string* error_out) {
  for (const auto& node : plan.nodes) {
    // Check if op starts with known prefixes
    if (node.op.rfind("core:", 0) == 0 || node.op.rfind("js:", 0) == 0) {
      continue;  // Known prefix
    }
    if (error_out) {
      *error_out = "Unknown op type: " + node.op;
    }
    return false;
  }
  return true;
}

bool PlanCompiler::ValidatePlanEnv(const Plan& plan, std::string* error_out) {
  const std::string& env = plan.meta.env;

  // Only enforce experimental restriction in prod
  if (env != "prod") {
    return true;
  }

  // Check each node for experimental stability
  for (const auto& node : plan.nodes) {
    const NodeSpec* spec = NodeRegistry::Instance().GetSpec(node.op);

    if (!spec) {
      // Node not registered - this is caught by ValidateOps, skip here
      continue;
    }

    if (spec->stability == Stability::kExperimental) {
      if (error_out) {
        *error_out = fmt::format(
          "Production plans cannot use experimental nodes. "
          "Node '{}' (op: '{}', namespace: '{}') has stability=experimental.",
          node.id, node.op, spec->namespace_path
        );
      }
      return false;
    }
  }

  return true;
}

}  // namespace ranking_dsl
