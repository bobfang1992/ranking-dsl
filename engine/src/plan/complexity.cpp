#include "plan/complexity.h"

#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace ranking_dsl {

ComplexityMetrics ComputeComplexityMetrics(const Plan& plan, int top_k) {
  ComplexityMetrics metrics;
  metrics.node_count = static_cast<int64_t>(plan.nodes.size());

  if (plan.nodes.empty()) {
    return metrics;
  }

  // Build graph structures
  std::unordered_map<std::string, const PlanNode*> node_map;
  std::unordered_map<std::string, std::vector<std::string>> adj;      // node -> dependents
  std::unordered_map<std::string, std::vector<std::string>> reverse;  // node -> inputs
  std::unordered_map<std::string, int64_t> out_degree;
  std::unordered_map<std::string, int64_t> in_degree;

  for (const auto& node : plan.nodes) {
    node_map[node.id] = &node;
    adj[node.id];  // Ensure entry
    reverse[node.id];
    in_degree[node.id] = static_cast<int64_t>(node.inputs.size());
    out_degree[node.id] = 0;

    for (const auto& input : node.inputs) {
      adj[input].push_back(node.id);
      reverse[node.id].push_back(input);
      metrics.edge_count++;
    }
  }

  // Compute out-degrees
  for (const auto& [id, deps] : adj) {
    out_degree[id] = static_cast<int64_t>(deps.size());
  }

  // Find fanout and fanin peaks
  for (const auto& node : plan.nodes) {
    metrics.fanout_peak = std::max(metrics.fanout_peak, out_degree[node.id]);
    metrics.fanin_peak = std::max(metrics.fanin_peak, in_degree[node.id]);
  }

  // Compute max depth using dynamic programming
  // depth[v] = longest path ending at v
  std::unordered_map<std::string, int64_t> depth;
  std::unordered_map<std::string, std::string> predecessor;  // For path reconstruction

  // Kahn's algorithm with depth tracking
  std::queue<std::string> queue;
  std::unordered_map<std::string, int64_t> remaining_in;
  for (const auto& node : plan.nodes) {
    remaining_in[node.id] = in_degree[node.id];
    depth[node.id] = 1;  // Base depth
    if (in_degree[node.id] == 0) {
      queue.push(node.id);
    }
  }

  std::string deepest_node;
  int64_t max_depth = 0;

  while (!queue.empty()) {
    std::string current = queue.front();
    queue.pop();

    if (depth[current] > max_depth) {
      max_depth = depth[current];
      deepest_node = current;
    }

    for (const auto& dep : adj[current]) {
      int64_t new_depth = depth[current] + 1;
      if (new_depth > depth[dep]) {
        depth[dep] = new_depth;
        predecessor[dep] = current;
      }
      remaining_in[dep]--;
      if (remaining_in[dep] == 0) {
        queue.push(dep);
      }
    }
  }

  metrics.max_depth = max_depth;

  // Reconstruct longest path (from deepest_node backwards)
  if (!deepest_node.empty()) {
    std::vector<std::string> path;
    std::string current = deepest_node;
    while (!current.empty()) {
      path.push_back(current);
      auto it = predecessor.find(current);
      current = (it != predecessor.end()) ? it->second : "";
    }
    std::reverse(path.begin(), path.end());
    metrics.longest_path = std::move(path);
  }

  // Collect top-K fanout nodes
  std::vector<ComplexityMetrics::NodeInfo> fanout_nodes;
  for (const auto& node : plan.nodes) {
    fanout_nodes.push_back({node.id, node.op, out_degree[node.id]});
  }
  std::sort(fanout_nodes.begin(), fanout_nodes.end(),
            [](const auto& a, const auto& b) { return a.degree > b.degree; });
  for (int i = 0; i < top_k && i < static_cast<int>(fanout_nodes.size()); ++i) {
    metrics.top_fanout.push_back(fanout_nodes[i]);
  }

  // Collect top-K fanin nodes
  std::vector<ComplexityMetrics::NodeInfo> fanin_nodes;
  for (const auto& node : plan.nodes) {
    fanin_nodes.push_back({node.id, node.op, in_degree[node.id]});
  }
  std::sort(fanin_nodes.begin(), fanin_nodes.end(),
            [](const auto& a, const auto& b) { return a.degree > b.degree; });
  for (int i = 0; i < top_k && i < static_cast<int>(fanin_nodes.size()); ++i) {
    metrics.top_fanin.push_back(fanin_nodes[i]);
  }

  return metrics;
}

int64_t ComputeComplexityScore(
    const ComplexityMetrics& metrics,
    double weight_n,
    double weight_d,
    double weight_fout,
    double weight_fin,
    double weight_e) {
  return static_cast<int64_t>(
      weight_n * metrics.node_count +
      weight_d * metrics.max_depth +
      weight_fout * metrics.fanout_peak +
      weight_fin * metrics.fanin_peak +
      weight_e * metrics.edge_count);
}

ComplexityCheckResult CheckComplexityBudget(
    const ComplexityMetrics& metrics,
    const ComplexityBudget& budget) {
  ComplexityCheckResult result;
  result.passed = true;
  result.has_warnings = false;

  std::vector<std::string> violations;
  std::vector<std::string> warnings;

  // Check hard limits
  if (budget.node_count_hard > 0 && metrics.node_count > budget.node_count_hard) {
    violations.push_back(fmt::format("node_count={} (hard_limit={})",
                                      metrics.node_count, budget.node_count_hard));
  }
  if (budget.max_depth_hard > 0 && metrics.max_depth > budget.max_depth_hard) {
    violations.push_back(fmt::format("max_depth={} (hard_limit={})",
                                      metrics.max_depth, budget.max_depth_hard));
  }
  if (budget.fanout_peak_hard > 0 && metrics.fanout_peak > budget.fanout_peak_hard) {
    violations.push_back(fmt::format("fanout_peak={} (hard_limit={})",
                                      metrics.fanout_peak, budget.fanout_peak_hard));
  }
  if (budget.fanin_peak_hard > 0 && metrics.fanin_peak > budget.fanin_peak_hard) {
    violations.push_back(fmt::format("fanin_peak={} (hard_limit={})",
                                      metrics.fanin_peak, budget.fanin_peak_hard));
  }

  // Check soft limits
  if (budget.edge_count_soft > 0 && metrics.edge_count > budget.edge_count_soft) {
    warnings.push_back(fmt::format("edge_count={} (soft_limit={})",
                                    metrics.edge_count, budget.edge_count_soft));
  }
  if (budget.complexity_score_soft > 0) {
    int64_t score = ComputeComplexityScore(
        metrics,
        budget.score_weights.node_count,
        budget.score_weights.max_depth,
        budget.score_weights.fanout_peak,
        budget.score_weights.fanin_peak,
        budget.score_weights.edge_count);
    if (score > budget.complexity_score_soft) {
      warnings.push_back(fmt::format("complexity_score={} (soft_limit={})",
                                      score, budget.complexity_score_soft));
    }
  }

  result.has_warnings = !warnings.empty();

  if (!violations.empty()) {
    result.passed = false;
    result.error_code = "PLAN_TOO_COMPLEX";

    std::ostringstream ss;
    ss << "PLAN_TOO_COMPLEX:\n";

    // All metrics
    ss << "  node_count=" << metrics.node_count;
    if (budget.node_count_hard > 0) {
      ss << " (hard_limit=" << budget.node_count_hard << ")";
    }
    ss << "\n";

    ss << "  edge_count=" << metrics.edge_count;
    if (budget.edge_count_soft > 0) {
      ss << " (soft_limit=" << budget.edge_count_soft << ")";
    }
    ss << "\n";

    ss << "  max_depth=" << metrics.max_depth;
    if (budget.max_depth_hard > 0) {
      ss << " (hard_limit=" << budget.max_depth_hard << ")";
    }
    ss << "\n";

    ss << "  fanout_peak=" << metrics.fanout_peak;
    if (budget.fanout_peak_hard > 0) {
      ss << " (hard_limit=" << budget.fanout_peak_hard << ")";
    }
    ss << "\n";

    ss << "  fanin_peak=" << metrics.fanin_peak;
    if (budget.fanin_peak_hard > 0) {
      ss << " (hard_limit=" << budget.fanin_peak_hard << ")";
    }
    ss << "\n";

    // Top fanout nodes
    if (!metrics.top_fanout.empty()) {
      ss << "Top fanout nodes:\n";
      for (const auto& node : metrics.top_fanout) {
        if (node.degree > 0) {
          ss << "  " << node.id << " " << node.op << " fanout=" << node.degree << "\n";
        }
      }
    }

    // Top fanin nodes
    if (!metrics.top_fanin.empty()) {
      ss << "Top fanin nodes:\n";
      for (const auto& node : metrics.top_fanin) {
        if (node.degree > 0) {
          ss << "  " << node.id << " " << node.op << " fanin=" << node.degree << "\n";
        }
      }
    }

    // Longest path
    if (!metrics.longest_path.empty()) {
      ss << "Longest path (len=" << metrics.longest_path.size() << "):\n  ";
      for (size_t i = 0; i < metrics.longest_path.size(); ++i) {
        if (i > 0) ss << " -> ";
        ss << metrics.longest_path[i];
        // Limit output for very long paths
        if (i >= 5 && i < metrics.longest_path.size() - 2) {
          ss << " -> ... -> " << metrics.longest_path.back();
          break;
        }
      }
      ss << "\n";
    }

    // Remediation hint
    ss << "Hint:\n";
    ss << "  Collapse repeated logic into 1-3 njs module nodes, or request a core C++ node.\n";
    ss << "  See docs/complexity-governance.md for guidance.";

    result.diagnostics = ss.str();
  }

  return result;
}

ComplexityBudget ComplexityBudget::Default() {
  ComplexityBudget budget;
  budget.node_count_hard = 2000;
  budget.max_depth_hard = 120;
  budget.fanout_peak_hard = 16;
  budget.fanin_peak_hard = 16;
  budget.edge_count_soft = 10000;
  budget.complexity_score_soft = 8000;
  return budget;
}

ComplexityBudget ComplexityBudget::Parse(const std::string& json_str, std::string* error_out) {
  try {
    auto j = nlohmann::json::parse(json_str);
    ComplexityBudget budget;

    if (j.contains("hard")) {
      const auto& hard = j["hard"];
      if (hard.contains("node_count")) {
        budget.node_count_hard = hard["node_count"].get<int64_t>();
      }
      if (hard.contains("max_depth")) {
        budget.max_depth_hard = hard["max_depth"].get<int64_t>();
      }
      if (hard.contains("fanout_peak")) {
        budget.fanout_peak_hard = hard["fanout_peak"].get<int64_t>();
      }
      if (hard.contains("fanin_peak")) {
        budget.fanin_peak_hard = hard["fanin_peak"].get<int64_t>();
      }
    }

    if (j.contains("soft")) {
      const auto& soft = j["soft"];
      if (soft.contains("edge_count")) {
        budget.edge_count_soft = soft["edge_count"].get<int64_t>();
      }
      if (soft.contains("complexity_score")) {
        budget.complexity_score_soft = soft["complexity_score"].get<int64_t>();
      }
    }

    if (j.contains("score_weights")) {
      const auto& sw = j["score_weights"];
      if (sw.contains("node_count")) {
        budget.score_weights.node_count = sw["node_count"].get<double>();
      }
      if (sw.contains("max_depth")) {
        budget.score_weights.max_depth = sw["max_depth"].get<double>();
      }
      if (sw.contains("fanout_peak")) {
        budget.score_weights.fanout_peak = sw["fanout_peak"].get<double>();
      }
      if (sw.contains("fanin_peak")) {
        budget.score_weights.fanin_peak = sw["fanin_peak"].get<double>();
      }
      if (sw.contains("edge_count")) {
        budget.score_weights.edge_count = sw["edge_count"].get<double>();
      }
    }

    return budget;
  } catch (const std::exception& e) {
    if (error_out) {
      *error_out = std::string("Failed to parse complexity budget: ") + e.what();
    }
    return ComplexityBudget();
  }
}

ComplexityBudget ComplexityBudget::LoadFromFile(const std::string& path, std::string* error_out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    if (error_out) {
      *error_out = "Failed to open complexity budget file: " + path;
    }
    return ComplexityBudget();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return Parse(buffer.str(), error_out);
}

}  // namespace ranking_dsl
