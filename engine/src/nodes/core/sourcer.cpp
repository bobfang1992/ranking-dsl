#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:sourcer - Generates candidate objects.
 *
 * MVP: Creates fake candidates with candidate_id and base score.
 *
 * Params:
 *   - name: string (sourcer name)
 *   - k: int (number of candidates to generate)
 */
class SourcerNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    int k = params.value("k", 100);
    std::string name = params.value("name", "default");

    CandidateBatch output;
    output.reserve(k);

    for (int i = 0; i < k; ++i) {
      Obj obj;
      // Set candidate ID
      obj = obj.Set(keys::id::CAND_CANDIDATE_ID, static_cast<int64_t>(i + 1));
      // Set base score (decreasing by rank for testing)
      obj = obj.Set(keys::id::SCORE_BASE, 1.0f - (static_cast<float>(i) / k));
      output.push_back(std::move(obj));
    }

    return output;
  }

  std::string TypeName() const override { return "core:sourcer"; }
};

REGISTER_NODE_RUNNER("core:sourcer", SourcerNode);

}  // namespace ranking_dsl
