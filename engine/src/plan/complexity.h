#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "plan/plan.h"

namespace ranking_dsl {

/**
 * Complexity metrics computed from a Plan DAG.
 */
struct ComplexityMetrics {
  int64_t node_count = 0;      // N = number of nodes
  int64_t edge_count = 0;      // E = number of edges
  int64_t max_depth = 0;       // D = longest path length
  int64_t fanout_peak = 0;     // max out-degree
  int64_t fanin_peak = 0;      // max in-degree

  // Top offenders for diagnostics
  struct NodeInfo {
    std::string id;
    std::string op;
    int64_t degree;
  };
  std::vector<NodeInfo> top_fanout;  // Top-K by out-degree
  std::vector<NodeInfo> top_fanin;   // Top-K by in-degree
  std::vector<std::string> longest_path;  // Node IDs on longest path
};

/**
 * Score weights for complexity score computation.
 */
struct ScoreWeights {
  double node_count = 1.0;
  double max_depth = 5.0;
  double fanout_peak = 2.0;
  double fanin_peak = 2.0;
  double edge_count = 0.5;
};

/**
 * Complexity budget limits.
 * A value of 0 means "no limit".
 */
struct ComplexityBudget {
  // Hard limits (compile fails if exceeded)
  int64_t node_count_hard = 0;
  int64_t max_depth_hard = 0;
  int64_t fanout_peak_hard = 0;
  int64_t fanin_peak_hard = 0;

  // Soft limits (warnings only)
  int64_t edge_count_soft = 0;
  int64_t complexity_score_soft = 0;

  // Score weights for complexity score computation
  ScoreWeights score_weights;

  // Parse from JSON string
  static ComplexityBudget Parse(const std::string& json_str, std::string* error_out = nullptr);

  // Parse from JSON file
  static ComplexityBudget LoadFromFile(const std::string& path, std::string* error_out = nullptr);

  // Default budget (used if no policy file provided)
  static ComplexityBudget Default();
};

/**
 * Result of complexity check.
 */
struct ComplexityCheckResult {
  bool passed = true;
  bool has_warnings = false;
  std::string error_code;  // e.g., "PLAN_TOO_COMPLEX"
  std::string diagnostics;  // Human-readable diagnostics
};

/**
 * Compute complexity metrics for a plan.
 */
ComplexityMetrics ComputeComplexityMetrics(const Plan& plan, int top_k = 5);

/**
 * Check if metrics are within budget.
 * Returns detailed diagnostics on failure.
 */
ComplexityCheckResult CheckComplexityBudget(
    const ComplexityMetrics& metrics,
    const ComplexityBudget& budget);

/**
 * Compute weighted complexity score.
 * S = a*N + b*D + c*F_out + d*F_in + e*E
 */
int64_t ComputeComplexityScore(
    const ComplexityMetrics& metrics,
    double weight_n = 1.0,
    double weight_d = 5.0,
    double weight_fout = 2.0,
    double weight_fin = 2.0,
    double weight_e = 0.5);

}  // namespace ranking_dsl
