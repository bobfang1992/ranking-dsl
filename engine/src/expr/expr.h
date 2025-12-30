#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "object/obj.h"
#include "object/value.h"

namespace ranking_dsl {

class KeyRegistry;

/**
 * Expression IR node types.
 */
struct ConstExpr {
  float value;
};

struct SignalExpr {
  int32_t key_id;
};

struct AddExpr;
struct MulExpr;
struct MinExpr;
struct MaxExpr;
struct CosExpr;
struct ClampExpr;
struct PenaltyExpr;

/**
 * Expression IR variant.
 */
using ExprNode = std::variant<
    ConstExpr,
    SignalExpr,
    std::unique_ptr<AddExpr>,
    std::unique_ptr<MulExpr>,
    std::unique_ptr<MinExpr>,
    std::unique_ptr<MaxExpr>,
    std::unique_ptr<CosExpr>,
    std::unique_ptr<ClampExpr>,
    std::unique_ptr<PenaltyExpr>>;

struct AddExpr {
  std::vector<ExprNode> args;
};

struct MulExpr {
  std::vector<ExprNode> args;
};

struct MinExpr {
  std::vector<ExprNode> args;
};

struct MaxExpr {
  std::vector<ExprNode> args;
};

struct CosExpr {
  ExprNode a;
  ExprNode b;
};

struct ClampExpr {
  ExprNode x;
  ExprNode lo;
  ExprNode hi;
};

struct PenaltyExpr {
  std::string name;
};

/**
 * Parse an expression from JSON.
 * Returns nullptr on parse error.
 */
ExprNode ParseExpr(const nlohmann::json& json, std::string* error_out = nullptr);

/**
 * Evaluate an expression against an Obj.
 * Returns the result as a float (expressions are expected to produce numeric results).
 *
 * @param expr The expression to evaluate
 * @param obj The object to read signals from
 * @param registry Key registry for penalty lookups
 * @return The evaluated result
 */
float EvalExpr(const ExprNode& expr, const Obj& obj, const KeyRegistry* registry = nullptr);

/**
 * Collect all key IDs referenced by an expression.
 */
std::vector<int32_t> CollectKeyIds(const ExprNode& expr);

}  // namespace ranking_dsl
