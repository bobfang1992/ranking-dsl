#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:features - Populates feature keys.
 *
 * MVP: Stub implementation that adds placeholder features.
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

    CandidateBatch output;
    output.reserve(input.size());

    for (const auto& obj : input) {
      Obj new_obj = obj;

      // MVP: Add placeholder values for requested features
      for (int32_t key_id : feature_keys) {
        if (key_id == keys::id::FEAT_FRESHNESS) {
          // Stub: random-ish freshness based on candidate ID
          auto id_opt = obj.Get(keys::id::CAND_CANDIDATE_ID);
          float freshness = 0.5f;
          if (id_opt) {
            if (auto* id = std::get_if<int64_t>(&*id_opt)) {
              freshness = static_cast<float>((*id % 100)) / 100.0f;
            }
          }
          new_obj = new_obj.Set(key_id, freshness);
        } else if (key_id == keys::id::FEAT_EMBEDDING ||
                   key_id == keys::id::FEAT_QUERY_EMBEDDING) {
          // Stub: placeholder embedding
          std::vector<float> embedding(128, 0.1f);
          new_obj = new_obj.Set(key_id, std::move(embedding));
        } else {
          // Default: set to 0.0f
          new_obj = new_obj.Set(key_id, 0.0f);
        }
      }

      output.push_back(std::move(new_obj));
    }

    return output;
  }

  std::string TypeName() const override { return "core:features"; }
};

REGISTER_NODE_RUNNER("core:features", FeaturesNode);

}  // namespace ranking_dsl
