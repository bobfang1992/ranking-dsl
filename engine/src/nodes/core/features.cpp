#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"

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
    auto* id_col = input.GetI64Column(keys::id::CAND_CANDIDATE_ID);

    for (int32_t key_id : feature_keys) {
      if (key_id == keys::id::FEAT_FRESHNESS) {
        // Create freshness column (F32)
        auto col = std::make_shared<F32Column>(row_count);
        for (size_t i = 0; i < row_count; ++i) {
          float freshness = 0.5f;
          if (id_col && !id_col->IsNull(i)) {
            int64_t id = id_col->Get(i);
            freshness = static_cast<float>((id % 100)) / 100.0f;
          }
          col->Set(i, freshness);
        }
        builder.AddF32Column(key_id, col);
      } else if (key_id == keys::id::FEAT_EMBEDDING ||
                 key_id == keys::id::FEAT_QUERY_EMBEDDING) {
        // Create embedding column (F32Vec with contiguous N*D storage)
        constexpr size_t dim = 128;
        auto col = std::make_shared<F32VecColumn>(row_count, dim);
        std::vector<float> embedding(dim, 0.1f);
        for (size_t i = 0; i < row_count; ++i) {
          col->Set(i, embedding);  // Same embedding for all (stub)
        }
        builder.AddF32VecColumn(key_id, col);
      } else {
        // Default: set to 0.0f (F32)
        auto col = std::make_shared<F32Column>(row_count);
        for (size_t i = 0; i < row_count; ++i) {
          col->Set(i, 0.0f);
        }
        builder.AddF32Column(key_id, col);
      }
    }

    return builder.Build();
  }

  std::string TypeName() const override { return "core:features"; }
};

// NodeSpec for core:features (v0.2.8+)
static NodeSpec CreateFeaturesNodeSpec() {
  NodeSpec spec;
  spec.op = "core:features";
  spec.namespace_path = "core.features";
  spec.stability = Stability::kStable;
  spec.doc = "Populates feature keys with computed or stub values. Supports f32 and f32vec features.";

  // Params schema (JSON Schema)
  spec.params_schema_json = R"({
    "type": "object",
    "properties": {
      "keys": {
        "type": "array",
        "items": {"type": "integer"},
        "description": "Array of key IDs to populate as features"
      }
    },
    "required": ["keys"]
  })";

  // Reads: candidate ID for feature computation
  spec.reads = {keys::id::CAND_CANDIDATE_ID};

  // Writes: param-derived from "keys" parameter
  spec.writes.kind = WritesDescriptor::Kind::kParamDerived;
  spec.writes.param_name = "keys";

  return spec;
}

REGISTER_NODE_RUNNER("core:features", FeaturesNode, CreateFeaturesNodeSpec());

}  // namespace ranking_dsl
