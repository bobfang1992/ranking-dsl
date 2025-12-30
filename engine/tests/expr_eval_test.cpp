#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include "expr/expr.h"
#include "object/obj.h"
#include "keys.h"

using namespace ranking_dsl;
using json = nlohmann::json;

TEST_CASE("Expr parsing", "[expr]") {
  SECTION("Parse const") {
    auto j = json::parse(R"({"op": "const", "value": 0.5})");
    std::string error;
    auto expr = ParseExpr(j, &error);

    REQUIRE(std::holds_alternative<ConstExpr>(expr));
    REQUIRE(std::get<ConstExpr>(expr).value == 0.5f);
  }

  SECTION("Parse signal") {
    auto j = json::parse(R"({"op": "signal", "key_id": 3001})");
    auto expr = ParseExpr(j);

    REQUIRE(std::holds_alternative<SignalExpr>(expr));
    REQUIRE(std::get<SignalExpr>(expr).key_id == 3001);
  }

  SECTION("Parse add") {
    auto j = json::parse(R"({
      "op": "add",
      "args": [
        {"op": "const", "value": 1},
        {"op": "const", "value": 2}
      ]
    })");
    auto expr = ParseExpr(j);

    REQUIRE(std::holds_alternative<std::unique_ptr<AddExpr>>(expr));
    const auto& add = std::get<std::unique_ptr<AddExpr>>(expr);
    REQUIRE(add->args.size() == 2);
  }

  SECTION("Parse mul") {
    auto j = json::parse(R"({
      "op": "mul",
      "args": [
        {"op": "const", "value": 0.7},
        {"op": "signal", "key_id": 3001}
      ]
    })");
    auto expr = ParseExpr(j);

    REQUIRE(std::holds_alternative<std::unique_ptr<MulExpr>>(expr));
  }

  SECTION("Parse penalty") {
    auto j = json::parse(R"({"op": "penalty", "name": "constraints"})");
    auto expr = ParseExpr(j);

    REQUIRE(std::holds_alternative<std::unique_ptr<PenaltyExpr>>(expr));
    const auto& penalty = std::get<std::unique_ptr<PenaltyExpr>>(expr);
    REQUIRE(penalty->name == "constraints");
  }
}

TEST_CASE("Expr evaluation", "[expr]") {
  SECTION("Eval const") {
    auto j = json::parse(R"({"op": "const", "value": 42})");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 42.0f);
  }

  SECTION("Eval signal") {
    auto j = json::parse(R"({"op": "signal", "key_id": 3001})");
    auto expr = ParseExpr(j);

    Obj obj = Obj().Set(3001, 0.75f);
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 0.75f);
  }

  SECTION("Eval signal missing key") {
    auto j = json::parse(R"({"op": "signal", "key_id": 9999})");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 0.0f);  // Default for missing
  }

  SECTION("Eval add") {
    auto j = json::parse(R"({
      "op": "add",
      "args": [
        {"op": "const", "value": 1.5},
        {"op": "const", "value": 2.5}
      ]
    })");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 4.0f);
  }

  SECTION("Eval mul") {
    auto j = json::parse(R"({
      "op": "mul",
      "args": [
        {"op": "const", "value": 3},
        {"op": "const", "value": 4}
      ]
    })");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 12.0f);
  }

  SECTION("Eval weighted sum") {
    // 0.7 * score_base + 0.3 * score_ml
    auto j = json::parse(R"({
      "op": "add",
      "args": [
        {"op": "mul", "args": [{"op": "const", "value": 0.7}, {"op": "signal", "key_id": 3001}]},
        {"op": "mul", "args": [{"op": "const", "value": 0.3}, {"op": "signal", "key_id": 3002}]}
      ]
    })");
    auto expr = ParseExpr(j);

    Obj obj = Obj().Set(3001, 1.0f).Set(3002, 0.5f);
    float result = EvalExpr(expr, obj);
    REQUIRE_THAT(result, Catch::Matchers::WithinRel(0.85f, 0.001f));
  }

  SECTION("Eval min") {
    auto j = json::parse(R"({
      "op": "min",
      "args": [
        {"op": "const", "value": 3},
        {"op": "const", "value": 1},
        {"op": "const", "value": 2}
      ]
    })");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 1.0f);
  }

  SECTION("Eval max") {
    auto j = json::parse(R"({
      "op": "max",
      "args": [
        {"op": "const", "value": 3},
        {"op": "const", "value": 1},
        {"op": "const", "value": 2}
      ]
    })");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 3.0f);
  }

  SECTION("Eval clamp") {
    auto j = json::parse(R"({
      "op": "clamp",
      "x": {"op": "const", "value": 1.5},
      "lo": {"op": "const", "value": 0},
      "hi": {"op": "const", "value": 1}
    })");
    auto expr = ParseExpr(j);

    Obj obj;
    float result = EvalExpr(expr, obj);
    REQUIRE(result == 1.0f);  // Clamped to hi
  }
}

TEST_CASE("Cosine similarity", "[expr][cos]") {
  SECTION("Identical vectors") {
    auto j = json::parse(R"({
      "op": "cos",
      "a": {"op": "signal", "key_id": 2001},
      "b": {"op": "signal", "key_id": 2002}
    })");
    auto expr = ParseExpr(j);

    std::vector<float> vec = {1.0f, 0.0f, 0.0f};
    Obj obj = Obj().Set(2001, vec).Set(2002, vec);

    float result = EvalExpr(expr, obj);
    REQUIRE_THAT(result, Catch::Matchers::WithinRel(1.0f, 0.001f));
  }

  SECTION("Orthogonal vectors") {
    auto j = json::parse(R"({
      "op": "cos",
      "a": {"op": "signal", "key_id": 2001},
      "b": {"op": "signal", "key_id": 2002}
    })");
    auto expr = ParseExpr(j);

    Obj obj = Obj()
                  .Set(2001, std::vector<float>{1.0f, 0.0f, 0.0f})
                  .Set(2002, std::vector<float>{0.0f, 1.0f, 0.0f});

    float result = EvalExpr(expr, obj);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.0f, 0.001f));
  }

  SECTION("Opposite vectors") {
    auto j = json::parse(R"({
      "op": "cos",
      "a": {"op": "signal", "key_id": 2001},
      "b": {"op": "signal", "key_id": 2002}
    })");
    auto expr = ParseExpr(j);

    Obj obj = Obj()
                  .Set(2001, std::vector<float>{1.0f, 0.0f})
                  .Set(2002, std::vector<float>{-1.0f, 0.0f});

    float result = EvalExpr(expr, obj);
    REQUIRE_THAT(result, Catch::Matchers::WithinRel(-1.0f, 0.001f));
  }

  SECTION("Missing vector returns 0") {
    auto j = json::parse(R"({
      "op": "cos",
      "a": {"op": "signal", "key_id": 2001},
      "b": {"op": "signal", "key_id": 2002}
    })");
    auto expr = ParseExpr(j);

    Obj obj = Obj().Set(2001, std::vector<float>{1.0f, 0.0f});  // Only one vector

    float result = EvalExpr(expr, obj);
    REQUIRE(result == 0.0f);
  }

  SECTION("Zero vector returns 0") {
    auto j = json::parse(R"({
      "op": "cos",
      "a": {"op": "signal", "key_id": 2001},
      "b": {"op": "signal", "key_id": 2002}
    })");
    auto expr = ParseExpr(j);

    Obj obj = Obj()
                  .Set(2001, std::vector<float>{0.0f, 0.0f})
                  .Set(2002, std::vector<float>{1.0f, 0.0f});

    float result = EvalExpr(expr, obj);
    REQUIRE(result == 0.0f);
  }
}

TEST_CASE("CollectKeyIds", "[expr]") {
  SECTION("Signal") {
    auto j = json::parse(R"({"op": "signal", "key_id": 3001})");
    auto expr = ParseExpr(j);
    auto ids = CollectKeyIds(expr);

    REQUIRE(ids.size() == 1);
    REQUIRE(ids[0] == 3001);
  }

  SECTION("Nested expression") {
    auto j = json::parse(R"({
      "op": "add",
      "args": [
        {"op": "mul", "args": [{"op": "const", "value": 0.7}, {"op": "signal", "key_id": 3001}]},
        {"op": "mul", "args": [{"op": "const", "value": 0.3}, {"op": "signal", "key_id": 3002}]}
      ]
    })");
    auto expr = ParseExpr(j);
    auto ids = CollectKeyIds(expr);

    REQUIRE(ids.size() == 2);
  }

  SECTION("Const only") {
    auto j = json::parse(R"({"op": "const", "value": 1})");
    auto expr = ParseExpr(j);
    auto ids = CollectKeyIds(expr);

    REQUIRE(ids.empty());
  }
}
