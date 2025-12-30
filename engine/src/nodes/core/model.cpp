#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:model - Runs a model and writes score.ml.
 *
 * MVP: Stub implementation that computes a simple score.
 *
 * Params:
 *   - name: string (model name)
 */
class ModelNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    std::string name = params.value("name", "default");

    CandidateBatch output;
    output.reserve(input.size());

    for (const auto& obj : input) {
      Obj new_obj = obj;

      // MVP: Compute a simple ML score based on base score and freshness
      float base_score = 0.0f;
      float freshness = 0.0f;

      auto base_opt = obj.Get(keys::id::SCORE_BASE);
      if (base_opt) {
        if (auto* f = std::get_if<float>(&*base_opt)) {
          base_score = *f;
        }
      }

      auto fresh_opt = obj.Get(keys::id::FEAT_FRESHNESS);
      if (fresh_opt) {
        if (auto* f = std::get_if<float>(&*fresh_opt)) {
          freshness = *f;
        }
      }

      // Simple weighted combination
      float ml_score = 0.6f * base_score + 0.4f * freshness;
      new_obj = new_obj.Set(keys::id::SCORE_ML, ml_score);

      output.push_back(std::move(new_obj));
    }

    return output;
  }

  std::string TypeName() const override { return "core:model"; }
};

REGISTER_NODE_RUNNER("core:model", ModelNode);

}  // namespace ranking_dsl
