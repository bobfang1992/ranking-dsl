#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:model - Runs a model and writes score.ml.
 *
 * MVP: Stub implementation that computes a simple score.
 * Uses BatchBuilder with COW - original columns are shared.
 *
 * Params:
 *   - name: string (model name)
 */
class ModelNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    size_t row_count = input.RowCount();
    if (row_count == 0) {
      return input;
    }

    // Get input columns (typed access)
    auto* base_col = input.GetF32Column(keys::id::SCORE_BASE);
    auto* fresh_col = input.GetF32Column(keys::id::FEAT_FRESHNESS);

    // Create ML score column
    auto ml_col = std::make_shared<F32Column>(row_count);

    for (size_t i = 0; i < row_count; ++i) {
      float base_score = 0.0f;
      float freshness = 0.0f;

      if (base_col && !base_col->IsNull(i)) {
        base_score = base_col->Get(i);
      }

      if (fresh_col && !fresh_col->IsNull(i)) {
        freshness = fresh_col->Get(i);
      }

      // Simple weighted combination
      float ml_score = 0.6f * base_score + 0.4f * freshness;
      ml_col->Set(i, ml_score);
    }

    // Use BatchBuilder for COW semantics
    BatchBuilder builder(input);
    builder.AddF32Column(keys::id::SCORE_ML, ml_col);

    return builder.Build();
  }

  std::string TypeName() const override { return "core:model"; }
};

REGISTER_NODE_RUNNER("core:model", ModelNode);

}  // namespace ranking_dsl
