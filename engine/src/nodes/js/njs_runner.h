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
  NjsCapabilities capabilities;

  static NjsMeta Parse(const nlohmann::json& j);
};

/**
 * Policy entry for a single njs module.
 */
struct NjsPolicyEntry {
  std::string name;
  std::string version;
  bool allow_io_csv_read = false;
};

/**
 * Engine-side policy for njs modules (default deny).
 * Modules must be explicitly allowlisted for IO capabilities.
 */
class NjsPolicy {
 public:
  NjsPolicy() = default;

  // Load policy from JSON file
  bool LoadFromFile(const std::string& path, std::string* error_out = nullptr);

  // Load policy from JSON string
  bool LoadFromJson(const std::string& json_str, std::string* error_out = nullptr);

  // Check if a module is allowed IO capabilities
  bool IsIoCsvReadAllowed(const std::string& name, const std::string& version) const;

  // Get the CSV assets base directory
  const std::string& CsvAssetsDir() const { return csv_assets_dir_; }

 private:
  std::vector<NjsPolicyEntry> entries_;
  std::string csv_assets_dir_ = "njs/assets/csv";  // Default assets directory
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
 * - IO capabilities via policy allowlist (default deny)
 *
 * Sandbox guarantees:
 * - No QuickJS std/os modules exposed
 * - No filesystem/network/process APIs
 * - IO only via ctx.io when capability enabled AND policy allows
 */
class NjsRunner : public NodeRunner {
 public:
  NjsRunner();
  ~NjsRunner() override;

  // Set the policy for IO capabilities (must be called before Run if IO needed)
  void SetPolicy(const NjsPolicy* policy) { policy_ = policy; }

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
  const NjsPolicy* policy_ = nullptr;
};

}  // namespace ranking_dsl
