#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "expr/expr.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:score_formula - Evaluates an expression and writes the result.
 *
 * Params:
 *   - expr: ExprIR (the expression to evaluate)
 *   - output_key_id: int32 (key to write result to, default: score.final)
 */
class ScoreFormulaNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    int32_t output_key = params.value("output_key_id", keys::id::SCORE_FINAL);

    // Parse the expression
    std::string error;
    ExprNode expr;
    if (params.contains("expr")) {
      expr = ParseExpr(params["expr"], &error);
    } else {
      // Default: just use base score
      expr = SignalExpr{keys::id::SCORE_BASE};
    }

    CandidateBatch output;
    output.reserve(input.size());

    for (const auto& obj : input) {
      float result = EvalExpr(expr, obj, ctx.registry);
      Obj new_obj = obj.Set(output_key, result);
      output.push_back(std::move(new_obj));
    }

    return output;
  }

  std::string TypeName() const override { return "core:score_formula"; }
};

REGISTER_NODE_RUNNER("core:score_formula", ScoreFormulaNode);

}  // namespace ranking_dsl
