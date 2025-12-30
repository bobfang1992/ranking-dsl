#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:merge - Merges and deduplicates candidates.
 *
 * Params:
 *   - dedup: "max_base" | "first" (deduplication strategy)
 */
class MergeNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    std::string dedup = params.value("dedup", "first");

    size_t row_count = input.RowCount();
    if (row_count == 0) {
      return ColumnBatch(0);
    }

    // Get the candidate_id and score columns
    auto id_col = input.GetColumn(keys::id::CAND_CANDIDATE_ID);
    auto score_col = input.GetColumn(keys::id::SCORE_BASE);

    // Track best row index for each candidate_id
    // Key: candidate_id, Value: row index
    std::unordered_map<int64_t, size_t> best_row;

    for (size_t i = 0; i < row_count; ++i) {
      // Get candidate ID
      int64_t id = 0;
      if (id_col) {
        const Value& val = id_col->Get(i);
        if (auto* p = std::get_if<int64_t>(&val)) {
          id = *p;
        } else {
          continue;  // Skip rows without valid ID
        }
      } else {
        continue;
      }

      auto it = best_row.find(id);
      if (it == best_row.end()) {
        best_row[id] = i;
      } else if (dedup == "max_base" && score_col) {
        // Compare scores
        float existing_score = 0.0f;
        float new_score = 0.0f;

        const Value& existing_val = score_col->Get(it->second);
        if (auto* f = std::get_if<float>(&existing_val)) {
          existing_score = *f;
        }

        const Value& new_val = score_col->Get(i);
        if (auto* f = std::get_if<float>(&new_val)) {
          new_score = *f;
        }

        if (new_score > existing_score) {
          best_row[id] = i;
        }
      }
      // "first" strategy: keep first encountered (default)
    }

    // Collect selected row indices
    std::vector<size_t> selected_rows;
    selected_rows.reserve(best_row.size());
    for (const auto& [_, row_idx] : best_row) {
      selected_rows.push_back(row_idx);
    }

    // Sort for deterministic output (optional)
    std::sort(selected_rows.begin(), selected_rows.end());

    // Create output batch with selected rows
    size_t out_row_count = selected_rows.size();
    ColumnBatch output(out_row_count);

    // Copy columns, selecting only the chosen rows
    for (int32_t key_id : input.ColumnKeys()) {
      auto src_col = input.GetColumn(key_id);
      if (!src_col) continue;

      auto dst_col = std::make_shared<Column>(out_row_count);
      for (size_t out_idx = 0; out_idx < out_row_count; ++out_idx) {
        size_t src_idx = selected_rows[out_idx];
        dst_col->Set(out_idx, src_col->Get(src_idx));
      }
      output.SetColumn(key_id, dst_col);
    }

    return output;
  }

  std::string TypeName() const override { return "core:merge"; }
};

REGISTER_NODE_RUNNER("core:merge", MergeNode);

}  // namespace ranking_dsl
