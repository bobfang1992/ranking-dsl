#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "expr/expr.h"
#include "object/batch_builder.h"

#include <nlohmann/json.hpp>

namespace ranking_dsl {

/**
 * core:score_formula - Evaluates an expression and writes the result.
 *
 * Uses columnar evaluation: reads input columns, writes output column.
 * Uses BatchBuilder with COW - original columns are shared.
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

    size_t row_count = input.RowCount();
    if (row_count == 0) {
      return input;
    }

    // Create output column
    auto output_col = std::make_shared<Column>(row_count);

    // Evaluate expression for each row using columnar API
    for (size_t i = 0; i < row_count; ++i) {
      float result = EvalExpr(expr, input, i, ctx.registry);
      output_col->Set(i, result);
    }

    // Use BatchBuilder for COW semantics
    BatchBuilder builder(input);
    builder.AddColumn(output_key, output_col);

    return builder.Build();
  }

  std::string TypeName() const override { return "core:score_formula"; }
};

REGISTER_NODE_RUNNER("core:score_formula", ScoreFormulaNode);

}  // namespace ranking_dsl
