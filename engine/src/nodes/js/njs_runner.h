#pragma once

#include <memory>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "nodes/node_runner.h"
#include "nodes/js/batch_context.h"

namespace ranking_dsl {

/**
 * Metadata parsed from an njs module's `meta` export.
 */
struct NjsMeta {
  std::string name;
  std::string version;
  std::set<int32_t> reads;
  std::set<int32_t> writes;
  nlohmann::json params_schema;
  NjsBudget budget;

  static NjsMeta Parse(const nlohmann::json& j);
};

/**
 * NjsRunner executes JavaScript njs modules.
 *
 * Supports two execution modes:
 * 1. Row-level: runBatch returns Obj[] with row-level modifications
 * 2. Column-level: runBatch uses ctx.batch.write* APIs and returns undefined
 *
 * Enforces:
 * - meta.writes for all write operations
 * - Budget limits (max_write_bytes, max_write_cells, max_set_per_obj)
 * - Type checks via KeyRegistry
 */
class NjsRunner : public NodeRunner {
 public:
  NjsRunner();
  ~NjsRunner() override;

  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override;

  std::string TypeName() const override { return "njs"; }

  // For testing: directly execute with parsed meta and function
  CandidateBatch RunWithMeta(const ExecContext& ctx,
                             const CandidateBatch& input,
                             const nlohmann::json& params,
                             const NjsMeta& meta,
                             std::function<void(BatchContext&, const nlohmann::json&)> column_fn);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ranking_dsl
