#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:features - Populates feature keys.
 *
 * MVP: Stub implementation that adds placeholder features.
 * Uses BatchBuilder with COW - original columns are shared.
 *
 * Params:
 *   - keys: int32[] (key IDs to populate)
 */
class FeaturesNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    std::vector<int32_t> feature_keys;
    if (params.contains("keys")) {
      for (const auto& k : params["keys"]) {
        feature_keys.push_back(k.get<int32_t>());
      }
    }

    size_t row_count = input.RowCount();
    if (row_count == 0 || feature_keys.empty()) {
      return input;  // No changes needed
    }

    // Use BatchBuilder for COW semantics
    BatchBuilder builder(input);

    // Get candidate_id column for feature computation
    auto id_col = input.GetColumn(keys::id::CAND_CANDIDATE_ID);

    for (int32_t key_id : feature_keys) {
      if (key_id == keys::id::FEAT_FRESHNESS) {
        // Create freshness column
        auto col = std::make_shared<Column>(row_count);
        for (size_t i = 0; i < row_count; ++i) {
          float freshness = 0.5f;
          if (id_col) {
            const Value& val = id_col->Get(i);
            if (auto* id = std::get_if<int64_t>(&val)) {
              freshness = static_cast<float>((*id % 100)) / 100.0f;
            }
          }
          col->Set(i, freshness);
        }
        builder.AddColumn(key_id, col);
      } else if (key_id == keys::id::FEAT_EMBEDDING ||
                 key_id == keys::id::FEAT_QUERY_EMBEDDING) {
        // Create embedding column
        auto col = std::make_shared<Column>(row_count);
        std::vector<float> embedding(128, 0.1f);
        for (size_t i = 0; i < row_count; ++i) {
          col->Set(i, embedding);  // Same embedding for all (stub)
        }
        builder.AddColumn(key_id, col);
      } else {
        // Default: set to 0.0f
        auto col = std::make_shared<Column>(row_count);
        for (size_t i = 0; i < row_count; ++i) {
          col->Set(i, 0.0f);
        }
        builder.AddColumn(key_id, col);
      }
    }

    return builder.Build();
  }

  std::string TypeName() const override { return "core:features"; }
};

REGISTER_NODE_RUNNER("core:features", FeaturesNode);

}  // namespace ranking_dsl
