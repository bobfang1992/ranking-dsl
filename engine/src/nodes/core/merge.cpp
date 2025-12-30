#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"

#include <algorithm>
#include <unordered_map>

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

    // Group by candidate_id
    std::unordered_map<int64_t, Obj> best;

    for (const auto& obj : input) {
      auto id_opt = obj.Get(keys::id::CAND_CANDIDATE_ID);
      if (!id_opt) continue;

      auto* id_ptr = std::get_if<int64_t>(&*id_opt);
      if (!id_ptr) continue;

      int64_t id = *id_ptr;

      auto it = best.find(id);
      if (it == best.end()) {
        best[id] = obj;
      } else if (dedup == "max_base") {
        // Keep the one with higher base score
        auto existing_score_opt = it->second.Get(keys::id::SCORE_BASE);
        auto new_score_opt = obj.Get(keys::id::SCORE_BASE);

        float existing_score = 0.0f;
        float new_score = 0.0f;

        if (existing_score_opt) {
          if (auto* f = std::get_if<float>(&*existing_score_opt)) {
            existing_score = *f;
          }
        }
        if (new_score_opt) {
          if (auto* f = std::get_if<float>(&*new_score_opt)) {
            new_score = *f;
          }
        }

        if (new_score > existing_score) {
          best[id] = obj;
        }
      }
      // "first" strategy: keep first encountered (already handled by default)
    }

    CandidateBatch output;
    output.reserve(best.size());
    for (auto& [_, obj] : best) {
      output.push_back(std::move(obj));
    }

    return output;
  }

  std::string TypeName() const override { return "core:merge"; }
};

REGISTER_NODE_RUNNER("core:merge", MergeNode);

}  // namespace ranking_dsl
