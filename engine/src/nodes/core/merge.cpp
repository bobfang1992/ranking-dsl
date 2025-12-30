#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"

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

    // Get typed columns for faster access
    auto* id_col = input.GetI64Column(keys::id::CAND_CANDIDATE_ID);
    auto* score_col = input.GetF32Column(keys::id::SCORE_BASE);

    // Track best row index for each candidate_id
    std::unordered_map<int64_t, size_t> best_row;

    for (size_t i = 0; i < row_count; ++i) {
      if (!id_col || id_col->IsNull(i)) {
        continue;  // Skip rows without valid ID
      }
      int64_t id = id_col->Get(i);

      auto it = best_row.find(id);
      if (it == best_row.end()) {
        best_row[id] = i;
      } else if (dedup == "max_base" && score_col) {
        float existing_score = score_col->IsNull(it->second) ? 0.0f : score_col->Get(it->second);
        float new_score = score_col->IsNull(i) ? 0.0f : score_col->Get(i);

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

    // Sort for deterministic output
    std::sort(selected_rows.begin(), selected_rows.end());

    // Create output batch with selected rows
    size_t out_row_count = selected_rows.size();
    ColumnBatch output(out_row_count);

    // Copy columns, selecting only the chosen rows
    for (int32_t key_id : input.ColumnKeys()) {
      auto src_col = input.GetColumn(key_id);
      if (!src_col) continue;

      // Create new column of same type
      auto dst_col = src_col->Clone();
      // Note: Clone creates a copy of all data, but we only want selected rows.
      // For now, create from scratch using GetValue/SetValue.

      // Create output column based on type
      TypedColumnPtr out_col;
      switch (src_col->Type()) {
        case ColumnType::F32:
          out_col = std::make_shared<F32Column>(out_row_count);
          break;
        case ColumnType::I64:
          out_col = std::make_shared<I64Column>(out_row_count);
          break;
        case ColumnType::Bool:
          out_col = std::make_shared<BoolColumn>(out_row_count);
          break;
        case ColumnType::String:
          out_col = std::make_shared<StringColumn>(out_row_count);
          break;
        case ColumnType::F32Vec: {
          auto* vec_col = static_cast<F32VecColumn*>(src_col.get());
          out_col = std::make_shared<F32VecColumn>(out_row_count, vec_col->Dim());
          break;
        }
        case ColumnType::Bytes:
          out_col = std::make_shared<BytesColumn>(out_row_count);
          break;
        default:
          continue;
      }

      for (size_t out_idx = 0; out_idx < out_row_count; ++out_idx) {
        size_t src_idx = selected_rows[out_idx];
        out_col->SetValue(out_idx, src_col->GetValue(src_idx));
      }
      output.SetColumn(key_id, out_col);
    }

    return output;
  }

  std::string TypeName() const override { return "core:merge"; }
};

REGISTER_NODE_RUNNER("core:merge", MergeNode);

}  // namespace ranking_dsl
