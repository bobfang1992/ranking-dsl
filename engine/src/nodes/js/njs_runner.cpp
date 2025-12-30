#include "nodes/js/njs_runner.h"

#include <stdexcept>

#include "nodes/registry.h"

namespace ranking_dsl {

// Parse NjsMeta from JSON
NjsMeta NjsMeta::Parse(const nlohmann::json& j) {
  NjsMeta meta;

  if (j.contains("name")) {
    meta.name = j["name"].get<std::string>();
  }
  if (j.contains("version")) {
    meta.version = j["version"].get<std::string>();
  }

  if (j.contains("reads") && j["reads"].is_array()) {
    for (const auto& key : j["reads"]) {
      if (key.is_number_integer()) {
        meta.reads.insert(key.get<int32_t>());
      }
    }
  }

  if (j.contains("writes") && j["writes"].is_array()) {
    for (const auto& key : j["writes"]) {
      if (key.is_number_integer()) {
        meta.writes.insert(key.get<int32_t>());
      }
    }
  }

  if (j.contains("params")) {
    meta.params_schema = j["params"];
  }

  if (j.contains("budget")) {
    const auto& budget = j["budget"];
    if (budget.contains("max_write_bytes")) {
      meta.budget.max_write_bytes = budget["max_write_bytes"].get<int64_t>();
    }
    if (budget.contains("max_write_cells")) {
      meta.budget.max_write_cells = budget["max_write_cells"].get<int64_t>();
    }
    if (budget.contains("max_set_per_obj")) {
      meta.budget.max_set_per_obj = budget["max_set_per_obj"].get<int64_t>();
    }
  }

  return meta;
}

// Implementation class (holds JS runtime state when implemented)
class NjsRunner::Impl {
 public:
  Impl() = default;
  ~Impl() = default;

  // TODO: Add QuickJS runtime here for full JS execution
};

NjsRunner::NjsRunner() : impl_(std::make_unique<Impl>()) {}

NjsRunner::~NjsRunner() = default;

CandidateBatch NjsRunner::Run(const ExecContext& ctx,
                              const CandidateBatch& input,
                              const nlohmann::json& params) {
  // Load module path from params
  if (!params.contains("module")) {
    throw std::runtime_error("njs node requires 'module' param");
  }

  std::string module_path = params["module"].get<std::string>();

  // TODO: Implement full JS module loading with QuickJS
  // For now, throw an error indicating JS runtime not yet implemented
  throw std::runtime_error(
      "njs JS runtime not yet implemented. Use RunWithMeta for testing.");
}

CandidateBatch NjsRunner::RunWithMeta(
    const ExecContext& ctx,
    const CandidateBatch& input,
    const nlohmann::json& params,
    const NjsMeta& meta,
    std::function<void(BatchContext&, const nlohmann::json&)> column_fn) {

  if (input.RowCount() == 0) {
    return input;
  }

  // Create builder for COW semantics
  BatchBuilder builder(input);

  // Create budget tracker (copy from meta)
  NjsBudget budget = meta.budget;

  // Create batch context with enforcement
  BatchContext batch_ctx(input, builder, ctx.registry, meta.writes, budget);

  // Execute the column-level function
  column_fn(batch_ctx, params);

  // If column writers were used, commit them
  if (batch_ctx.HasColumnWrites()) {
    batch_ctx.Commit();
  }

  // Build and return the result
  return builder.Build();
}

// Register the node runner
REGISTER_NODE_RUNNER("njs", NjsRunner);

}  // namespace ranking_dsl
