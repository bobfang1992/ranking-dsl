#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"

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

    // Create typed columns directly
    auto id_column = std::make_shared<I64Column>(k);
    auto score_column = std::make_shared<F32Column>(k);

    for (int i = 0; i < k; ++i) {
      // Set candidate ID
      id_column->Set(i, static_cast<int64_t>(i + 1));
      // Set base score (decreasing by rank for testing)
      score_column->Set(i, 1.0f - (static_cast<float>(i) / k));
    }

    // Build the batch with columns
    ColumnBatch output(k);
    output.SetColumn(keys::id::CAND_CANDIDATE_ID, id_column);
    output.SetColumn(keys::id::SCORE_BASE, score_column);

    return output;
  }

  std::string TypeName() const override { return "core:sourcer"; }
};

// NodeSpec for core:sourcer (v0.2.8+)
static NodeSpec CreateSourcerNodeSpec() {
  NodeSpec spec;
  spec.op = "core:sourcer";
  spec.namespace_path = "core.sourcer";
  spec.stability = Stability::kStable;
  spec.doc = "Generates candidate objects from a source. Creates fake candidates with IDs and base scores for testing.";

  // Params schema (JSON Schema)
  spec.params_schema_json = R"({
    "type": "object",
    "properties": {
      "name": {
        "type": "string",
        "description": "Name of the sourcer"
      },
      "k": {
        "type": "integer",
        "description": "Number of candidates to generate",
        "minimum": 1,
        "default": 100
      }
    },
    "required": ["name"]
  })";

  // Reads: nothing (sources generate from scratch)
  spec.reads = {};

  // Writes: static list of keys
  spec.writes.kind = WritesDescriptor::Kind::kStatic;
  spec.writes.static_keys = {keys::id::CAND_CANDIDATE_ID, keys::id::SCORE_BASE};

  return spec;
}

REGISTER_NODE_RUNNER("core:sourcer", SourcerNode, CreateSourcerNodeSpec());

}  // namespace ranking_dsl
