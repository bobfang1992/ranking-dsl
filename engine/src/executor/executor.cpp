#include "executor/executor.h"

#include <chrono>
#include <unordered_map>

#include <fmt/format.h>

#include "keys/registry.h"
#include "logging/trace.h"
#include "nodes/node_runner.h"
#include "nodes/registry.h"

namespace ranking_dsl {

Executor::Executor(const KeyRegistry& registry) : registry_(registry) {}

CandidateBatch Executor::Execute(const CompiledPlan& plan, std::string* error_out) {
  ExecContext ctx;
  ctx.registry = &registry_;

  // Map node ID to its output
  std::unordered_map<std::string, CandidateBatch> outputs;

  // Build node lookup
  std::unordered_map<std::string, const NodeSpec*> node_by_id;
  for (const auto& node : plan.plan.nodes) {
    node_by_id[node.id] = &node;
  }

  // Execute in topological order
  for (const auto& node_id : plan.topo_order) {
    const auto* spec = node_by_id[node_id];
    if (!spec) {
      if (error_out) {
        *error_out = "Node not found: " + node_id;
      }
      return CandidateBatch(0);
    }

    // Create runner
    auto runner = NodeRegistry::Instance().Create(spec->op);
    if (!runner) {
      if (error_out) {
        *error_out = "Unknown op: " + spec->op;
      }
      return CandidateBatch(0);
    }

    // Gather input batch
    // For now, we take the first input or an empty batch
    // The merge node handles combining multiple batches
    CandidateBatch input(0);
    if (!spec->inputs.empty()) {
      auto it = outputs.find(spec->inputs[0]);
      if (it != outputs.end()) {
        input = it->second;
      }
    }

    // Run node with tracing
    auto start = std::chrono::high_resolution_clock::now();

    Tracer::LogNodeStart(plan.plan.name, node_id, spec->op);

    CandidateBatch output = runner->Run(ctx, input, spec->params);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    Tracer::LogNodeEnd(plan.plan.name, node_id, spec->op,
                       duration_ms, input.RowCount(), output.RowCount());

    outputs[node_id] = std::move(output);
  }

  // Return output of last node
  if (!plan.topo_order.empty()) {
    return outputs[plan.topo_order.back()];
  }

  return CandidateBatch(0);
}

}  // namespace ranking_dsl
